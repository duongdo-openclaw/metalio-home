// Simulator implementation of app/platform.h: libcurl on a worker thread, results drained on
// the UI thread by sim_platform_poll().
#include "platform_sim.h"

#include <curl/curl.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "../app/platform.h"

namespace platform {
namespace {

struct Job {
    uint32_t id;
    HttpRequest req;
    bool download = false;
    std::string path;
    Progress progress;
    HttpDone done;
    std::atomic<bool> cancelled{false};
};

struct Event {  // delivered on the UI thread
    std::shared_ptr<Job> job;
    bool final = false;
    HttpResponse resp;
    uint64_t done = 0, total = 0;
};

Config g_cfg;
std::string g_sd = "sdcard";
TouchSample g_touch;
std::string g_touch_info = "?";

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<std::shared_ptr<Job>> g_jobs;
std::map<uint32_t, std::shared_ptr<Job>> g_live;
std::deque<Event> g_events;
uint32_t g_next_id = 1;
std::vector<std::thread> g_workers;
bool g_stop = false;

void Post(Event e) {
    std::lock_guard<std::mutex> l(g_mu);
    g_events.push_back(std::move(e));
}

struct Ctx {
    Job* job;
    std::string* body;
    FILE* file;
    size_t limit;
    bool too_big = false;
    uint64_t last_progress_ms = 0;
};

size_t OnData(char* p, size_t sz, size_t n, void* u) {
    auto* c = static_cast<Ctx*>(u);
    size_t len = sz * n;
    if (c->file) return std::fwrite(p, 1, len, c->file);
    if (c->body->size() + len > c->limit) { c->too_big = true; return 0; }
    c->body->append(p, len);
    return len;
}

int OnProgress(void* u, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* c = static_cast<Ctx*>(u);
    if (c->job->cancelled) return 1;
    if (c->job->download && c->job->progress) {
        uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - c->last_progress_ms > 250) {
            c->last_progress_ms = now;
            Event e;
            std::lock_guard<std::mutex> l(g_mu);
            auto it = g_live.find(c->job->id);
            if (it != g_live.end()) {
                e.job = it->second;
                e.done = (uint64_t)dlnow;
                e.total = (uint64_t)dltotal;
                g_events.push_back(std::move(e));
            }
        }
    }
    return 0;
}

void Run(const std::shared_ptr<Job>& job) {
    HttpResponse r;
    CURL* h = curl_easy_init();
    std::string body;
    FILE* f = nullptr;
    std::string part = job->path + ".part";
    if (job->download) Log("[net] download %s -> %s", job->req.url.c_str(), job->path.c_str());
    if (job->download) {
        f = std::fopen(part.c_str(), "wb");
        if (!f) { r.error = "không ghi được " + part; Post({job, true, r}); curl_easy_cleanup(h); return; }
    }
    Ctx ctx{job.get(), &body, f, job->req.max_body};
    struct curl_slist* hdr = nullptr;
    if (!job->req.content_type.empty()) hdr = curl_slist_append(hdr, ("Content-Type: " + job->req.content_type).c_str());
    if (!job->req.token.empty()) hdr = curl_slist_append(hdr, ("X-Kiosk-Token: " + job->req.token).c_str());
    for (const auto& kv : job->req.headers) hdr = curl_slist_append(hdr, (kv.first + ": " + kv.second).c_str());
    curl_easy_setopt(h, CURLOPT_URL, job->req.url.c_str());
    curl_easy_setopt(h, CURLOPT_USERAGENT, "metalio-home-sim/0.1 (ESP32-S31 simulator)");
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "https");  // like esp_http_client: https never downgrades
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
    if (!job->download) curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, (long)job->req.timeout_ms);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, OnData);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    if (hdr) curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
    if (job->req.method == "POST") {
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, job->req.body.c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)job->req.body.size());
    }
    CURLcode rc = curl_easy_perform(h);
    long code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    char* ctype = nullptr;
    curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &ctype);
    std::string content_type = ctype ? ctype : "";
    if (f) std::fclose(f);
    if (rc == CURLE_OK) {
        r.status = (int)code;
        r.body = std::move(body);
        // Drive answers 200 + an HTML page when a file is not public or the quota is exhausted.
        if (job->download && r.ok() && content_type.find("text/html") != std::string::npos) {
            r.status = 0;
            r.error = "Drive trả về trang HTML thay vì tệp (tệp không công khai hoặc hết lượt tải)";
        }
    } else if (ctx.too_big) {
        r.error = "phản hồi quá lớn";
    } else {
        r.error = curl_easy_strerror(rc);
    }
    if (job->download) {
        if (r.ok() && std::rename(part.c_str(), job->path.c_str()) != 0) { r.status = 0; r.error = "không đổi tên tệp"; }
        if (!r.ok()) std::remove(part.c_str());
    }
    if (hdr) curl_slist_free_all(hdr);
    curl_easy_cleanup(h);
    Post({job, true, r});
}

void Worker() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> l(g_mu);
            g_cv.wait(l, [] { return g_stop || !g_jobs.empty(); });
            if (g_stop) return;
            job = g_jobs.front();
            g_jobs.pop_front();
        }
        if (!job->cancelled) Run(job);
    }
}

uint32_t Enqueue(std::shared_ptr<Job> job) {
    std::lock_guard<std::mutex> l(g_mu);
    job->id = g_next_id++;
    g_live[job->id] = job;
    g_jobs.push_back(job);
    g_cv.notify_one();
    return job->id;
}

}  // namespace

uint32_t Http(const HttpRequest& req, HttpDone done) {
    auto job = std::make_shared<Job>();
    job->req = req;
    job->done = std::move(done);
    return Enqueue(job);
}

uint32_t Download(const std::string& url, const std::string& path, Progress progress, HttpDone done) {
    auto job = std::make_shared<Job>();
    job->req.url = url;
    job->download = true;
    job->path = path;
    job->progress = std::move(progress);
    job->done = std::move(done);
    return Enqueue(job);
}

void Cancel(uint32_t id) {
    std::lock_guard<std::mutex> l(g_mu);
    auto it = g_live.find(id);
    if (it != g_live.end()) {
        it->second->cancelled = true;
        g_live.erase(it);
    }
}

const Config& GetConfig() { return g_cfg; }

bool FileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

TouchSample LastTouch() { return g_touch; }

std::vector<std::string> ListFiles(const std::string& dir, const std::vector<std::string>& exts) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n.empty() || n[0] == '.') continue;
        std::string lower = n;
        for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
        for (const auto& x : exts) {
            if (lower.size() >= x.size() && lower.compare(lower.size() - x.size(), x.size(), x) == 0) {
                if (FileExists(dir + "/" + n)) out.push_back(n);
                break;
            }
        }
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool ReadText(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool WriteText(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool RemoveFile(const std::string& path) { return std::remove(path.c_str()) == 0; }

uint64_t FileSize(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0;
}

int BatteryPercent() {   // simulator: 86 %, or METALIO_SIM_BATTERY=0..100 to see other levels
    const char* e = std::getenv("METALIO_SIM_BATTERY");
    return e ? std::max(0, std::min(100, std::atoi(e))) : 86;
}
bool Charging() { return std::getenv("METALIO_SIM_CHARGING") != nullptr; }
namespace { int g_charge_ma = 1000; }
BatteryInfo Battery() {
    BatteryInfo b;
    b.percent = BatteryPercent();
    b.charging = Charging();
    b.voltage_mv = 3450 + b.percent * 7 + (b.charging ? 120 : 0);   // rough Li-ion curve
    b.current_ma = b.charging ? g_charge_ma * 9 / 10 : -85;
    return b;
}
int ChargeCurrentMa() { return g_charge_ma; }
bool SetChargeCurrentMa(int ma) {
    if (ma != 500 && ma != 1000 && ma != 1500) return false;   // same whitelist as the firmware
    g_charge_ma = ma;
    Log("[device] dòng sạc %d mA", ma);
    return true;
}
std::string WifiName() { return "Nha-Wifi"; }

std::string MusicDir() { return g_sd + "/music"; }
std::string RecordingsDir() { return g_sd + "/recordings"; }
std::string FontsDir() { return g_sd + "/fonts"; }
std::string StateDir() { return g_sd + "/.metalio"; }
std::string SdRoot() { return g_sd; }
std::string DictDir() { return g_sd + "/dict"; }
std::string VocabDir() { return g_sd + "/vocab"; }
std::string NotesDir() { return g_sd + "/notes"; }

std::vector<DirEntry> ListDir(const std::string& dir) {
    std::vector<DirEntry> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n.empty() || n[0] == '.') continue;
        struct stat st;
        if (::stat((dir + "/" + n).c_str(), &st) != 0) continue;
        DirEntry de;
        de.name = n;
        de.dir = S_ISDIR(st.st_mode);
        if (!de.dir && !S_ISREG(st.st_mode)) continue;
        de.size = de.dir ? 0 : (uint64_t)st.st_size;
        out.push_back(de);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    return out;
}

bool MakeDir(const std::string& path) {
    ::mkdir(path.c_str(), 0755);
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool ReadRange(const std::string& path, uint64_t offset, size_t len, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(len);
    bool ok = fseeko(f, (off_t)offset, SEEK_SET) == 0;
    size_t n = ok ? std::fread(&out[0], 1, len, f) : 0;
    std::fclose(f);
    out.resize(n);
    return ok;
}
const char* TouchDriverInfo() { return g_touch_info.c_str(); }

}  // namespace platform

// ---- simulator hooks ----

void sim_platform_init(const platform::Config& cfg, const std::string& sd_root) {
    platform::g_cfg = cfg;
    platform::g_sd = sd_root;
    // The SD-format feature deletes everything under the card root. Mark a directory as a
    // simulated card only when the simulator created it (missing or empty), never an existing folder.
    const std::string marker = sd_root + "/.metalio/sim-sdcard";
    bool empty = true;
    if (DIR* d = ::opendir(sd_root.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string n = e->d_name;
            if (n != "." && n != ".." && n != "books") { empty = false; break; }
            if (n == "books") {  // main() pre-creates books/; it only counts as empty if books/ is empty too
                DIR* b = ::opendir((sd_root + "/books").c_str());
                int count = 0;
                while (b && ::readdir(b)) ++count;
                if (b) ::closedir(b);
                if (count > 2) { empty = false; break; }
            }
        }
        ::closedir(d);
    }
    for (const char* d : {"/music", "/recordings", "/.metalio", "/fonts", "/dict", "/vocab", "/notes"}) ::mkdir((sd_root + d).c_str(), 0755);
    if (empty && !platform::FileExists(marker)) platform::WriteText(marker, "metalio simulator SD card\n");
    if (!platform::FileExists(marker))
        platform::Log("[sim] %s không phải thẻ nhớ do giả lập tạo: chức năng định dạng sẽ bị từ chối", sd_root.c_str());
    curl_global_init(CURL_GLOBAL_DEFAULT);
    for (int i = 0; i < 2; ++i) platform::g_workers.emplace_back(platform::Worker);
}

void sim_platform_shutdown() {
    {
        std::lock_guard<std::mutex> l(platform::g_mu);
        platform::g_stop = true;
        for (auto& kv : platform::g_live) kv.second->cancelled = true;
    }
    platform::g_cv.notify_all();
    for (auto& t : platform::g_workers) t.join();
    curl_global_cleanup();
}

int sim_platform_busy() {
    std::lock_guard<std::mutex> l(platform::g_mu);
    return (int)platform::g_live.size();
}

void sim_platform_poll() {
    std::deque<platform::Event> ev;
    {
        std::lock_guard<std::mutex> l(platform::g_mu);
        ev.swap(platform::g_events);
    }
    for (auto& e : ev) {
        auto& job = e.job;
        bool live;
        {
            std::lock_guard<std::mutex> l(platform::g_mu);
            live = platform::g_live.count(job->id) && !job->cancelled;
            if (e.final) platform::g_live.erase(job->id);
        }
        if (!live) continue;
        if (!e.final) { if (job->progress) job->progress(e.done, e.total); continue; }
        if (job->done) job->done(e.resp);
    }
}

void sim_set_touch(const platform::TouchSample& s) { platform::g_touch = s; }
void sim_set_touch_info(const std::string& s) { platform::g_touch_info = s; }
