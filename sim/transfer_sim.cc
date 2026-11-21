// Wi-Fi file transfer server for the simulator: a phone or PC on the same network opens
// http://<ip>:<port>/ and uploads books (EPUB/TXT) or music (MP3/WAV) straight to the SD card.
// Same routes the device will serve with esp_http_server. Runs only while the transfer screen is open.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <ctime>
#include <random>

#include "../app/drive_folder.h"
#include "../app/online_library.h"
#include "../app/ai_client.h"
#include "../app/platform.h"
#include "platform_sim.h"

namespace {

std::atomic<bool> g_run{false};
int g_listen = -1;
int g_port = 0;
std::thread g_thread;
std::mutex g_mu;
std::deque<platform::TransferEvent> g_events;
std::function<void(const platform::TransferEvent&)> g_on_event;
std::string g_sd = "sdcard";
// Share mode (read-only): set by ShareStart, cleared by TransferStart/TransferStop.
struct Share {
    bool on = false;
    std::string token, title;
    std::vector<platform::ShareItem> items;
} g_share;
std::mutex g_share_mu;
constexpr uint64_t kMaxUpload = 300ull * 1024 * 1024;

void Emit(platform::TransferEvent e) {
    std::lock_guard<std::mutex> l(g_mu);
    g_events.push_back(std::move(e));
}

struct DirSpec { const char* dir; std::vector<std::string> exts; bool upload; };
const std::vector<DirSpec>& Dirs() {
    static const std::vector<DirSpec> d = {
        {"books", {".epub", ".txt"}, true},
        {"music", {".mp3", ".wav"}, true},
        {"fonts", {".ttf", ".otf"}, true},
        {"dict", {".ifo", ".idx", ".dict", ".dz", ".gz", ".tsv"}, true},
        {"vocab", {".tsv"}, true},
        {"notes", {".md"}, false},        // exported notes: download only
        {"recordings", {".wav"}, false},  // read-only from the web
    };
    return d;
}

const DirSpec* Spec(const std::string& dir) {
    for (const auto& d : Dirs()) if (dir == d.dir) return &d;
    return nullptr;
}

std::string DirPath(const std::string& dir) { return Spec(dir) ? g_sd + "/" + dir : ""; }

bool HasExt(const std::string& name, const std::vector<std::string>& exts) {
    std::string l = name;
    for (auto& c : l) c = (char)std::tolower((unsigned char)c);
    for (const auto& x : exts) if (l.size() > x.size() && l.compare(l.size() - x.size(), x.size(), x) == 0) return true;
    return false;
}

bool AllowedExt(const std::string& dir, const std::string& name) {
    const DirSpec* d = Spec(dir);
    return d && d->upload && HasExt(name, d->exts);
}

std::string UrlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}

std::string Query(const std::string& target, const std::string& key) {
    size_t q = target.find('?');
    if (q == std::string::npos) return "";
    std::string qs = target.substr(q + 1);
    size_t p = 0;
    while (p <= qs.size()) {
        size_t amp = qs.find('&', p);
        std::string kv = qs.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) return UrlDecode(kv.substr(eq + 1));
        if (amp == std::string::npos) break;
        p = amp + 1;
    }
    return "";
}

std::string JsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

void Send(int fd, int code, const char* type, const std::string& body, const std::string& extra = "") {
    const char* reason = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 404 ? "Not Found" : code == 413 ? "Payload Too Large" : "Error";
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\nContent-Type: " + type +
                    "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n" + extra + "\r\n";
    ::send(fd, h.data(), h.size(), MSG_NOSIGNAL);
    ::send(fd, body.data(), body.size(), MSG_NOSIGNAL);
}

const char* kPage = R"HTML(<!doctype html><html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Chuyển file · Metalio</title>
<style>
:root{--ink:#1b1b1a;--paper:#f4f2ec;--rule:#cfccc3;--muted:#5d5b55}
*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.5 Georgia,"Noto Serif",serif}
main{max-width:760px;margin:0 auto;padding:24px 16px 48px}
h1{font-size:28px;margin:0 0 4px}p.sub{color:var(--muted);margin:0 0 20px}
.drop{border:2px dashed var(--ink);border-radius:12px;padding:24px 16px;text-align:center;background:#fff}
.drop.on{background:#e9e6dc}button,label.btn{font:600 15px system-ui,sans-serif;border:2px solid var(--ink);background:var(--ink);color:#fff;border-radius:8px;padding:10px 16px;cursor:pointer}
button.ghost{background:#fff;color:var(--ink)}h2{font-size:20px;margin:28px 0 6px;border-bottom:1px solid var(--rule);padding-bottom:6px}
h2 small{font:13px system-ui,sans-serif;color:var(--muted);margin-left:8px}
ul{list-style:none;margin:0;padding:0}li{display:flex;gap:12px;align-items:center;padding:8px 0;border-bottom:1px solid var(--rule);font-family:system-ui,sans-serif;font-size:15px}
li span.n{flex:1;min-width:0;overflow-wrap:anywhere}li span.s{color:var(--muted);font-variant-numeric:tabular-nums;white-space:nowrap}
#log{font:14px ui-monospace,monospace;color:var(--muted);margin-top:12px;white-space:pre-wrap;text-align:left}
progress{width:100%;height:10px;margin-top:12px}.kinds{font:14px system-ui,sans-serif;color:var(--muted);margin-top:8px}
form{display:grid;gap:8px;font-family:system-ui,sans-serif;background:#fff;border:1px solid var(--rule);border-radius:12px;padding:14px;margin-top:8px}
input[type=text],input[type=url]{font:16px system-ui,sans-serif;padding:10px;border:2px solid var(--ink);border-radius:8px;width:100%}
</style></head><body><main>
<h1>Chuyển file vào máy đọc sách</h1><p class="sub">Giữ màn “Chuyển file” mở trên máy trong lúc gửi. File tự vào đúng thư mục trên thẻ nhớ theo loại.</p>
<div class="drop" id="drop"><p>Kéo thả file vào đây</p><label class="btn">Chọn file<input id="pick" type="file" multiple hidden accept=".epub,.txt,.mp3,.wav,.ttf,.otf,.ifo,.idx,.dict,.dz,.gz,.tsv"></label>
<div class="kinds">Sách: EPUB, TXT · Nhạc: MP3, WAV · Phông chữ: TTF, OTF · Từ điển StarDict: .ifo .idx .dict .dict.dz · Bộ từ vựng: .tsv</div>
<progress id="bar" value="0" max="100" hidden></progress><div id="log"></div></div>
<h2>Thư viện online<small>link thư mục Google Drive công khai</small></h2>
<ul id="libs"></ul>
<form id="libform"><input id="lname" type="text" placeholder="Tên thư viện, ví dụ: Sách của nhà"><input id="llink" type="url" placeholder="https://drive.google.com/drive/folders/…" required>
<button type="submit">Thêm thư viện</button></form>
<h2 id="ai">Trợ lý AI<small>khoá API lưu trên thẻ nhớ của máy</small></h2><p id="aicur" class="kinds"></p>
<form id="aiform"><select id="aiprov" style="font:16px system-ui;padding:10px;border:2px solid #1b1b1a;border-radius:8px">
<option value="openai">ChatGPT (OpenAI)</option><option value="gemini">Gemini (Google)</option><option value="claude">Claude (Anthropic)</option>
<option value="deepseek">DeepSeek</option><option value="grok">Grok (xAI)</option><option value="mistral">Mistral</option><option value="groq">Groq</option>
<option value="openrouter">OpenRouter</option><option value="compatible">Máy chủ riêng (Ollama…)</option></select>
<input id="aikey" type="text" placeholder="Khoá API (sk-…, AIza…)" autocomplete="off"><input id="aimodel" type="text" placeholder="Model (để trống = mặc định)">
<input id="aiurl" type="text" placeholder="Địa chỉ máy chủ riêng, ví dụ http://192.168.1.10:11434/v1">
<button type="submit">Lưu vào máy</button></form>
<div id="lists"></div>
</main><script>
const $=id=>document.getElementById(id),log=t=>{$('log').textContent=t+'\n'+$('log').textContent};
const kb=n=>n>1048576?(n/1048576).toFixed(1)+' MB':Math.max(1,Math.round(n/1024))+' KB';
const KINDS=[['books','Sách',/\.(epub|txt)$/],['music','Nhạc',/\.(mp3|wav)$/],['fonts','Phông chữ',/\.(ttf|otf)$/],
 ['dict','Từ điển',/\.(ifo|idx|dict|dz|gz)$/],['vocab','Bộ từ vựng',/\.tsv$/],['notes','Ghi chú (xuất Markdown)',null],['recordings','Ghi âm',null]];
function dirFor(n){n=n.toLowerCase();if(/^.*\.tsv$/.test(n))return /tu-dien|dict/.test(n)?'dict':'vocab';for(const k of KINDS)if(k[2]&&k[2].test(n))return k[0];return''}
function row(d,f){const li=document.createElement('li');const n=document.createElement('span');n.className='n';n.textContent=f.name;
 const s=document.createElement('span');s.className='s';s.textContent=kb(f.size);const a=document.createElement('a');a.textContent='Tải';a.href='/api/download?dir='+d+'&name='+encodeURIComponent(f.name);
 const del=document.createElement('button');del.className='ghost';del.textContent='Xoá';del.onclick=async()=>{if(del.dataset.c!=='1'){del.dataset.c='1';del.textContent='Chắc chắn?';return}
 await fetch('/api/delete?dir='+d+'&name='+encodeURIComponent(f.name),{method:'POST'});refresh()};li.append(n,s,a,del);return li}
async function refresh(){const j=await (await fetch('/api/files')).json();const box=$('lists');box.innerHTML='';
 for(const [d,title] of KINDS){const h=document.createElement('h2');h.textContent=title;const sm=document.createElement('small');sm.textContent=d+'/';h.append(sm);
  const ul=document.createElement('ul');if(!j[d]||!j[d].length){ul.innerHTML='<li><span class="n">(trống)</span></li>'}else for(const f of j[d])ul.append(row(d,f));box.append(h,ul)}
 const L=await (await fetch('/api/libraries')).json();const ul=$('libs');ul.innerHTML='';if(!L.length)ul.innerHTML='<li><span class="n">(chưa có)</span></li>';
 L.forEach((l,i)=>{const li=document.createElement('li');const n=document.createElement('span');n.className='n';n.textContent=l.name+' · '+l.link;
  const del=document.createElement('button');del.className='ghost';del.textContent='Xoá';del.onclick=async()=>{if(del.dataset.c!=='1'){del.dataset.c='1';del.textContent='Chắc chắn?';return}
  await fetch('/api/libraries/delete?i='+i,{method:'POST'});setTimeout(refresh,400)};li.append(n,del);ul.append(li)})}
function send(file){return new Promise(res=>{const d=dirFor(file.name);if(!d){log('Bỏ qua '+file.name+': định dạng không hỗ trợ');return res()}
 const x=new XMLHttpRequest();x.open('POST','/api/upload?dir='+d+'&name='+encodeURIComponent(file.name));$('bar').hidden=false;
 x.upload.onprogress=e=>{if(e.lengthComputable)$('bar').value=e.loaded*100/e.total};
 x.onload=()=>{log((x.status==200?'Đã gửi ':'Lỗi '+x.status+' ')+file.name+' → '+d+'/');res()};x.onerror=()=>{log('Mất kết nối khi gửi '+file.name);res()};x.send(file)})}
async function sendAll(files){for(const f of files)await send(f);$('bar').hidden=true;refresh()}
$('pick').onchange=e=>sendAll([...e.target.files]);const dz=$('drop');
dz.ondragover=e=>{e.preventDefault();dz.classList.add('on')};dz.ondragleave=()=>dz.classList.remove('on');
dz.ondrop=e=>{e.preventDefault();dz.classList.remove('on');sendAll([...e.dataTransfer.files])};
$('aiform').onsubmit=async e=>{e.preventDefault();const q='provider='+$('aiprov').value+'&key='+encodeURIComponent($('aikey').value)+'&model='+encodeURIComponent($('aimodel').value)+'&url='+encodeURIComponent($('aiurl').value);
 const r=await fetch('/api/ai?'+q,{method:'POST'});log(r.ok?'Đã lưu cài đặt trợ lý AI vào máy':'Lỗi lưu trợ lý AI');if(r.ok){$('aikey').value='';setTimeout(aiLoad,500)}};
async function aiLoad(){try{const a=await (await fetch('/api/ai')).json();$('aicur').textContent='Đang dùng: '+a.name+' · khoá '+a.key+(a.model?' · model '+a.model:'');$('aiprov').value=a.provider;$('aimodel').value=a.model||'';$('aiurl').value=a.url||''}catch(e){}}
aiLoad();if(location.hash=='#ai')setTimeout(()=>$('ai').scrollIntoView(),300);
$('libform').onsubmit=async e=>{e.preventDefault();const r=await fetch('/api/libraries/add?name='+encodeURIComponent($('lname').value)+'&link='+encodeURIComponent($('llink').value),{method:'POST'});
 const j=await r.json();log(r.ok?'Đã thêm thư viện '+($('lname').value||''):'Lỗi: '+j.error);if(r.ok){$('lname').value='';$('llink').value=''}setTimeout(refresh,400)};refresh();
</script></body></html>)HTML";

std::string ListJson() {
    std::string out = "{";
    bool first = true;
    for (const auto& d : Dirs()) {
        out += std::string(first ? "" : ",") + "\"" + d.dir + "\":[";
        first = false;
        auto files = platform::ListFiles(DirPath(d.dir), d.exts);
        for (size_t k = 0; k < files.size(); ++k)
            out += std::string(k ? "," : "") + "{\"name\":\"" + JsonEscape(files[k]) + "\",\"size\":" +
                   std::to_string(platform::FileSize(DirPath(d.dir) + "/" + files[k])) + "}";
        out += "]";
    }
    return out + "}";
}

std::string LibrariesJson() {
    std::string out = "[";
    auto v = online::Sources();
    for (size_t i = 0; i < v.size(); ++i)
        out += std::string(i ? "," : "") + "{\"name\":\"" + JsonEscape(v[i].name) + "\",\"link\":\"" + JsonEscape(v[i].link) + "\"}";
    return out + "]";
}

std::string HtmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;"; else if (c == '"') o += "&quot;"; else o += c;
    }
    return o;
}

std::string SizeText(uint64_t b) {
    char buf[32];
    if (b >= 1048576) std::snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%llu KB", (unsigned long long)std::max<uint64_t>(1, b / 1024));
    return buf;
}

// /s/<token>/            page with download links
// /s/<token>/list.json   {"title":…,"items":[{"name":…,"size":…,"path":"f/0"}]} for another Metalio
// /s/<token>/f/<index>   the file itself
void HandleShare(int fd, const std::string& method, const std::string& path) {
    Share sh;
    {
        std::lock_guard<std::mutex> l(g_share_mu);
        sh = g_share;
    }
    const std::string base = "/s/" + sh.token + "/";
    if (method != "GET" || path.compare(0, base.size(), base) != 0) { Send(fd, 404, "text/plain", "not found"); return; }
    const std::string rest = path.substr(base.size());
    if (rest.empty()) {
        std::string li;
        for (size_t i = 0; i < sh.items.size(); ++i)
            li += "<li><span class=n>" + HtmlEscape(sh.items[i].name) + "</span><span class=s>" + SizeText(sh.items[i].size) +
                  "</span><a class=b href=\"f/" + std::to_string(i) + "\" download=\"" + HtmlEscape(sh.items[i].name) + "\">Tải về</a></li>";
        std::string page = "<!doctype html><html lang=vi><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>" + HtmlEscape(sh.title) + "</title><style>body{margin:0;background:#f4f2ec;color:#1b1b1a;font:16px/1.5 Georgia,serif}"
            "main{max-width:680px;margin:0 auto;padding:24px 16px}h1{font-size:26px;margin:0 0 4px}p{color:#5d5b55;margin:0 0 18px}"
            "ul{list-style:none;padding:0;margin:0}li{display:flex;gap:12px;align-items:center;padding:12px 0;border-bottom:1px solid #cfccc3;font-family:system-ui,sans-serif}"
            ".n{flex:1;min-width:0;overflow-wrap:anywhere}.s{color:#5d5b55;white-space:nowrap}.b{background:#1b1b1a;color:#fff;border-radius:8px;padding:8px 14px;text-decoration:none;font-weight:600}</style></head>"
            "<body><main><h1>" + HtmlEscape(sh.title) + "</h1><p>Chia sẻ từ máy đọc sách Metalio · " + std::to_string(sh.items.size()) +
            " tệp · chỉ tải về, không ai sửa hay xoá được.</p><ul>" + li + "</ul></main></body></html>";
        Send(fd, 200, "text/html; charset=utf-8", page);
        return;
    }
    if (rest == "list.json") {
        std::string out = "{\"title\":\"" + JsonEscape(sh.title) + "\",\"items\":[";
        for (size_t i = 0; i < sh.items.size(); ++i)
            out += std::string(i ? "," : "") + "{\"name\":\"" + JsonEscape(sh.items[i].name) + "\",\"size\":" + std::to_string(sh.items[i].size) +
                   ",\"path\":\"f/" + std::to_string(i) + "\"}";
        Send(fd, 200, "application/json", out + "]}");
        return;
    }
    if (rest.compare(0, 2, "f/") == 0) {
        std::string idx = rest.substr(2);
        if (idx.empty() || idx.size() > 5 || idx.find_first_not_of("0123456789") != std::string::npos) { Send(fd, 404, "text/plain", "not found"); return; }
        size_t i = (size_t)std::stoul(idx);
        std::string data;
        if (i >= sh.items.size() || !platform::ReadText(sh.items[i].path, data)) { Send(fd, 404, "text/plain", "not found"); return; }
        Send(fd, 200, "application/octet-stream", data, "Content-Disposition: attachment\r\n");
        Emit({"share_download", "", sh.items[i].name, (uint64_t)data.size(), true, ""});
        return;
    }
    Send(fd, 404, "text/plain", "not found");
}

void Handle(int fd) {
    std::string head;
    char buf[8192];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos && head.size() < 16384) {
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return;
        head.append(buf, (size_t)n);
        header_end = head.find("\r\n\r\n");
    }
    if (header_end == std::string::npos) return;
    std::string body_start = head.substr(header_end + 4);
    head.resize(header_end);
    std::string method = head.substr(0, head.find(' '));
    size_t t0 = head.find(' ') + 1;
    std::string target = head.substr(t0, head.find(' ', t0) - t0);
    std::string path = target.substr(0, target.find('?'));
    uint64_t length = 0;
    for (const char* key : {"\r\nContent-Length:", "\r\ncontent-length:"}) {
        size_t p = head.find(key);
        if (p != std::string::npos) length = std::strtoull(head.c_str() + p + std::strlen(key), nullptr, 10);
    }

    bool sharing;
    {
        std::lock_guard<std::mutex> l(g_share_mu);
        sharing = g_share.on;
    }
    if (sharing) { HandleShare(fd, method, path); return; }   // share mode: nothing else is served
    if (method == "GET" && path == "/") { Send(fd, 200, "text/html; charset=utf-8", kPage); return; }
    if (method == "GET" && path == "/api/files") { Send(fd, 200, "application/json", ListJson()); return; }
    if (method == "GET" && path == "/api/libraries") { Send(fd, 200, "application/json", LibrariesJson()); return; }
    // Library changes are applied on the UI thread (TransferEvent "library_add" / "library_delete").
    if (method == "POST" && path == "/api/libraries/add") {
        std::string link = Query(target, "link"), name = Query(target, "name");
        if (drive::ParseLink(link).empty() || link.size() < 10 || link.size() > 600 || name.size() > 120) {
            Send(fd, 400, "application/json", "{\"error\":\"link không phải thư mục Google Drive\"}");
            return;
        }
        Emit({"library_add", "", name, 0, true, link});
        Send(fd, 200, "application/json", "{\"ok\":true}");
        return;
    }
    if (method == "GET" && path == "/api/ai") {   // current AI settings, key masked (never sent back in full)
        ai::Config c = ai::Load();
        Send(fd, 200, "application/json", std::string("{\"provider\":\"") + ai::Info(c.provider).key + "\",\"name\":\"" +
             JsonEscape(ai::ProviderName(c.provider)) + "\",\"key\":\"" + JsonEscape(ai::MaskKey(c.api_key)) + "\",\"model\":\"" +
             JsonEscape(c.model) + "\",\"url\":\"" + JsonEscape(c.base_url) + "\"}");
        return;
    }
    if (method == "POST" && path == "/api/ai") {   // applied on the UI thread ("ai_config")
        std::string prov = Query(target, "provider"), key = Query(target, "key"), model = Query(target, "model"), url = Query(target, "url");
        if (prov.empty() || prov.size() > 20 || key.size() > 400 || model.size() > 120 || url.size() > 300) {
            Send(fd, 400, "application/json", "{\"error\":\"dữ liệu không hợp lệ\"}");
            return;
        }
        Emit({"ai_config", prov, model, 0, true, key + "\n" + url});
        Send(fd, 200, "application/json", "{\"ok\":true}");
        return;
    }
    if (method == "POST" && path == "/api/libraries/delete") {
        std::string i = Query(target, "i");
        if (i.empty() || i.size() > 3 || i.find_first_not_of("0123456789") != std::string::npos) { Send(fd, 400, "application/json", "{\"error\":\"sai chỉ số\"}"); return; }
        Emit({"library_delete", "", i, 0, true, ""});
        Send(fd, 200, "application/json", "{\"ok\":true}");
        return;
    }

    std::string dir = Query(target, "dir"), raw = Query(target, "name");
    std::string name = drive::SafeFileName(raw);
    std::string base = DirPath(dir);
    if (base.empty() || raw.empty() || name != raw || raw.find("..") != std::string::npos) {
        Send(fd, 400, "application/json", "{\"error\":\"tên hoặc thư mục không hợp lệ\"}");
        return;
    }
    std::string file = base + "/" + name;

    if (method == "GET" && path == "/api/download") {
        std::string data;
        if (!platform::ReadText(file, data)) { Send(fd, 404, "text/plain", "not found"); return; }
        Send(fd, 200, "application/octet-stream", data, "Content-Disposition: attachment\r\n");
        return;
    }
    if (method == "POST" && path == "/api/delete") {
        bool ok = platform::RemoveFile(file);
        Emit({"delete", dir, name, 0, ok, ""});
        Send(fd, ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"không có tệp\"}");
        return;
    }
    if (method == "POST" && path == "/api/upload") {
        if (!AllowedExt(dir, name)) { Send(fd, 400, "application/json", "{\"error\":\"định dạng không hỗ trợ\"}"); return; }
        if (length == 0 || length > kMaxUpload) { Send(fd, 413, "application/json", "{\"error\":\"tệp quá lớn hoặc rỗng\"}"); return; }
        std::string part = file + ".part";
        FILE* f = std::fopen(part.c_str(), "wb");
        if (!f) { Send(fd, 500, "application/json", "{\"error\":\"không ghi được thẻ nhớ\"}"); return; }
        uint64_t got = std::fwrite(body_start.data(), 1, std::min<uint64_t>(body_start.size(), length), f);
        while (got < length && g_run) {
            ssize_t n = ::recv(fd, buf, (size_t)std::min<uint64_t>(sizeof buf, length - got), 0);
            if (n <= 0) break;
            got += std::fwrite(buf, 1, (size_t)n, f);
        }
        std::fclose(f);
        bool ok = got == length && std::rename(part.c_str(), file.c_str()) == 0;
        if (!ok) std::remove(part.c_str());
        Emit({"upload", dir, name, got, ok, ok ? "" : "bị ngắt giữa chừng"});
        Send(fd, ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"bị ngắt giữa chừng\"}");
        return;
    }
    Send(fd, 404, "text/plain", "not found");
}

void Loop() {
    while (g_run) {
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        int fd = ::accept(g_listen, reinterpret_cast<sockaddr*>(&peer), &len);
        if (fd < 0) continue;
        timeval tv{15, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        Handle(fd);
        ::close(fd);
    }
}

}  // namespace

void sim_transfer_set_sd(const std::string& sd) { g_sd = sd; }

void sim_transfer_poll() {
    std::deque<platform::TransferEvent> ev;
    {
        std::lock_guard<std::mutex> l(g_mu);
        ev.swap(g_events);
    }
    for (auto& e : ev) if (g_on_event) g_on_event(e);
}

namespace platform {

bool TransferStart(TransferMode mode, std::function<void(const TransferEvent&)> on_event, TransferInfo& info, std::string& err) {
    TransferStop();
    // Hotspot: the device would start its own access point (esp_wifi AP, 192.168.4.1). The simulator
    // shows the same credentials but keeps serving on the PC's network so the page stays reachable.
    if (mode == TransferMode::Wifi && !SimDeviceState().wifi) { err = "Wi-Fi đang tắt"; return false; }
    std::string& url = info.url;
    info.ssid.clear();
    info.password.clear();
    if (mode == TransferMode::Hotspot) {
        info.ssid = "Metalio-4Plus-7A2C";
        info.password = "doc" + std::to_string(10000 + (unsigned)std::time(nullptr) % 90000);
    }
    g_listen = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    for (g_port = 8088; g_port < 8100; ++g_port) {
        a.sin_port = htons((uint16_t)g_port);
        if (::bind(g_listen, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) break;
    }
    if (g_port >= 8100 || ::listen(g_listen, 4) != 0) {
        err = "không mở được cổng mạng";
        ::close(g_listen);
        g_listen = -1;
        return false;
    }
    timeval tv{0, 300000};  // accept() wakes up regularly so Stop() is quick
    setsockopt(g_listen, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    g_on_event = std::move(on_event);
    g_run = true;
    g_thread = std::thread(Loop);
    url = "http://" + sim_lan_address() + ":" + std::to_string(g_port) + "/";
    Log("[transfer] listening on %s", url.c_str());
    return true;
}

void TransferStop() {
    {
        std::lock_guard<std::mutex> l(g_share_mu);
        g_share = Share{};
    }
    if (!g_run) return;
    g_run = false;
    if (g_thread.joinable()) g_thread.join();
    if (g_listen >= 0) ::close(g_listen);
    g_listen = -1;
    g_on_event = nullptr;
    Log("[transfer] stopped");
}

bool TransferRunning() { return g_run; }

bool ShareStart(TransferMode mode, const std::string& title, const std::vector<ShareItem>& items,
                std::function<void(const TransferEvent&)> on_event, TransferInfo& info, std::string& err) {
    if (!TransferStart(mode, std::move(on_event), info, err)) return false;
    std::random_device rd;
    static const char* abc = "abcdefghjkmnpqrstuvwxyz23456789";
    std::string token;
    for (int i = 0; i < 8; ++i) token += abc[rd() % 31];
    {
        std::lock_guard<std::mutex> l(g_share_mu);
        g_share.on = true;
        g_share.token = token;
        g_share.title = title;
        g_share.items = items;
    }
    info.url += "s/" + token + "/";
    Log("[share] %zu tệp tại %s", items.size(), info.url.c_str());
    return true;
}

}  // namespace platform
