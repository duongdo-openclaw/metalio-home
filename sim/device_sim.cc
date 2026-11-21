// Simulator stand-ins for device services: frontlight, black depth, haptics, full refresh, storage,
// radios and device info. Visual effects are applied by sim/main.cpp through SimDeviceState().
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "../app/ui_common.h"

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "../app/platform.h"
#include "../app/settings.h"
#include "platform_sim.h"

namespace {
SimDevice g_dev;
std::string g_sd_root = "sdcard";

// ---- SD "format" for the simulator: empties the simulated card directory ----
std::atomic<bool> g_formatting{false};
std::thread g_format_thread;
std::mutex g_fmu;
struct FormatEvent { bool final; int pct; std::string stage; bool ok; std::string err; };
std::deque<FormatEvent> g_fevents;
platform::FormatProgress g_fprogress;
platform::FormatDone g_fdone;

void PushF(FormatEvent e) {
    std::lock_guard<std::mutex> l(g_fmu);
    g_fevents.push_back(std::move(e));
}

// Refuses anything that is not clearly a simulator SD directory. The simulated card is a
// folder on the developer's disk, so a wrong path here would delete real files.
bool SafeSdRoot(const std::string& root, std::string& real, std::string& why) {
    char buf[PATH_MAX];
    if (!realpath(root.c_str(), buf)) { why = "không tìm thấy thư mục thẻ nhớ"; return false; }
    real = buf;
    const char* home = std::getenv("HOME");
    char hbuf[PATH_MAX];
    std::string home_real = home && realpath(home, hbuf) ? hbuf : "";
    int depth = 0;
    for (char c : real) if (c == '/') ++depth;
    struct stat st;
    if (real == "/" || real == home_real || depth < 3) { why = "đường dẫn thẻ nhớ không an toàn: " + real; return false; }
    if (stat((real + "/.metalio/sim-sdcard").c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        why = "thư mục không phải thẻ nhớ do giả lập tạo, từ chối xoá: " + real;
        return false;
    }
    return true;
}

std::string g_walk_root;
size_t g_walk_total = 0, g_walk_done = 0;

int CountCb(const char*, const struct stat*, int, struct FTW*) { ++g_walk_total; return 0; }

int RemoveCb(const char* path, const struct stat*, int type, struct FTW* ftw) {
    if (ftw->level == 0) return 0;                       // keep the card root itself
    if (std::string(path).rfind(g_walk_root + "/", 0) != 0) return -1;  // never leave the root
    int rc = (type == FTW_DP) ? rmdir(path) : unlink(path);  // FTW_PHYS: symlinks are removed, not followed
    ++g_walk_done;
    if (g_walk_done % 8 == 0 && g_walk_total)
        PushF({false, 10 + (int)(g_walk_done * 80 / g_walk_total), "Đang xoá dữ liệu cũ…", true, ""});
    return rc == 0 ? 0 : -1;
}

void FormatWorker(std::string root, bool keep) {
    std::string real, why;
    if (!SafeSdRoot(root, real, why)) { PushF({true, 0, "", false, why}); g_formatting = false; return; }
    PushF({false, 3, "Kiểm tra thẻ nhớ…", true, ""});
    std::string owner, sets;
    if (keep) {
        platform::ReadText(real + "/.metalio/owner.json", owner);
        platform::ReadText(real + "/.metalio/settings.json", sets);
    }
    g_walk_root = real;
    g_walk_total = g_walk_done = 0;
    nftw(real.c_str(), CountCb, 32, FTW_PHYS | FTW_MOUNT);
    PushF({false, 10, "Đang xoá dữ liệu cũ…", true, ""});
    int rc = nftw(real.c_str(), RemoveCb, 32, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
    if (rc != 0) { PushF({true, 0, "", false, "không xoá được một số tệp"}); g_formatting = false; return; }
    PushF({false, 92, "Tạo hệ thống tệp FAT32…", true, ""});
    std::this_thread::sleep_for(std::chrono::milliseconds(600));   // a real f_mkfs takes a few seconds
    for (const char* d : {"/books", "/music", "/recordings", "/.metalio"}) ::mkdir((real + d).c_str(), 0755);
    platform::WriteText(real + "/.metalio/sim-sdcard", "metalio simulator SD card\n");  // still the simulated card
    if (keep) {
        if (!owner.empty()) platform::WriteText(real + "/.metalio/owner.json", owner);
        if (!sets.empty()) platform::WriteText(real + "/.metalio/settings.json", sets);
    }
    PushF({false, 100, "Xong", true, ""});
    std::fprintf(stderr, "[device] đã định dạng thẻ nhớ giả lập %s (%zu mục, giữ cài đặt: %s)\n", real.c_str(),
                 g_walk_done, keep ? "có" : "không");
    PushF({true, 100, "", true, ""});
    g_formatting = false;
}
}

void sim_device_poll() {
    std::deque<FormatEvent> ev;
    {
        std::lock_guard<std::mutex> l(g_fmu);
        ev.swap(g_fevents);
    }
    for (auto& e : ev) {
        if (!e.final) { if (g_fprogress) g_fprogress(e.pct, e.stage); continue; }
        if (g_format_thread.joinable()) g_format_thread.join();
        auto done = g_fdone;
        g_fprogress = nullptr;
        g_fdone = nullptr;
        if (done) done(e.ok, e.err);
    }
}

SimDevice& SimDeviceState() { return g_dev; }
void sim_device_set_sd(const std::string& sd) { g_sd_root = sd; }

std::string sim_lan_address() {
    std::string best;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return "127.0.0.1";
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, buf, sizeof buf);
        std::string ip = buf;
        // Prefer the home LAN (192.168.x / 10.x) over docker/tailscale interfaces.
        if (ip.rfind("192.168.", 0) == 0) { best = ip; break; }
        if (best.empty() && ip.rfind("10.", 0) == 0) best = ip;
    }
    freeifaddrs(list);
    return best.empty() ? "127.0.0.1" : best;
}

namespace platform {

void ApplySettings(const settings::Values& v) {
    g_dev.light_mode = (int)v.light;
    g_dev.brightness = v.brightness;
    g_dev.black_depth = v.black_depth;
    g_dev.wifi = v.wifi;
    g_dev.dirty = true;
    std::fprintf(stderr, "[device] đèn=%s %d%% · độ đậm đen=%d · wifi=%s · bt=%s · usb=%s\n",
                 settings::LightName(v.light), v.brightness, v.black_depth, v.wifi ? "bật" : "tắt",
                 v.bt_page_turner ? "bật" : "tắt", v.usb_disk ? "bật" : "tắt");
}

void Vibrate(int strength) {
    if (strength > 0) std::fprintf(stderr, "[device] rung mức %d\n", strength);
}

void PowerOff() {
    // The device sends 15 pulses on P13 and the PMIC cuts power. The simulator keeps running.
    std::fprintf(stderr, "[device] giữ phím nguồn 3 giây: tắt máy (giả lập không tắt)\n");
    ui::Toast("Giữ nguồn 3 giây: máy thật sẽ tắt nguồn ở đây.", 2500);
}

void FullRefresh() {
    g_dev.flash_frames = 2;  // main loop shows black then the page again, like a GC16 refresh
    std::fprintf(stderr, "[device] làm mới toàn màn\n");
}

uint64_t g_du = 0;
int DuCb(const char*, const struct stat* st, int type, struct FTW*) {
    if (type == FTW_F) g_du += (uint64_t)st->st_size;
    return 0;
}

StorageInfo Storage() {
    // A 32 GB card, filled by what is really in the simulated SD folder (not the PC's disk).
    StorageInfo s;
    struct stat st;
    if (stat(g_sd_root.c_str(), &st) != 0) return s;
    g_du = 0;
    nftw(g_sd_root.c_str(), DuCb, 32, FTW_PHYS | FTW_MOUNT);
    s.present = true;
    s.total = 32ull * 1000 * 1000 * 1000 * 29 / 30;   // usable FAT32 space of a "32 GB" card ≈ 29.7 GiB
    s.free = s.total > g_du ? s.total - g_du : 0;
    return s;
}

void StorageFormat(bool keep_settings, FormatProgress progress, FormatDone done) {
    if (g_formatting) { if (done) done(false, "đang định dạng"); return; }
    if (g_format_thread.joinable()) g_format_thread.join();
    g_formatting = true;
    g_fprogress = std::move(progress);
    g_fdone = std::move(done);
    g_format_thread = std::thread(FormatWorker, g_sd_root, keep_settings);
}

bool StorageFormatting() { return g_formatting; }

std::vector<WifiNetwork> WifiScan() {
    if (!g_dev.wifi) return {};
    return {{WifiName(), -48, true, true}};  // simulator: only the network the PC is on
}

std::vector<std::string> BluetoothScan() { return {}; }  // no BLE radio in the simulator
bool UsbDiskSupported() { return false; }

DeviceInfo GetDeviceInfo() {
    return {"Metalio E-Ink4-Plus", "ESP32-S31 · 16 MB flash", "ED047TC2 4.7\" · 1216×684 · 297 ppi",
            "(giả lập)", "metalio-home 0.4 (giả lập)"};
}

}  // namespace platform
