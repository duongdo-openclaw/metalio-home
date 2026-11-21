// "Chia sẻ": send one book or a whole shelf to people nearby, share an online library, and
// receive books from another Metalio.
//   Books:   the device serves a read-only page (random address, download only) and shows its QR
//            code. Phones download in the browser; another Metalio uses "Nhận sách".
//            Works on the home Wi-Fi or on the reader's own hotspot (no router needed).
//   Library: QR code of the Google Drive folder link (open on a phone, or add on another reader).
//   Receive: type the address shown on the sharing device, pick the books, download to books/.
#include <cJSON.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <memory>

#include "drive_folder.h"
#include "icons.h"
#include "library.h"
#include "platform.h"
#include "screens.h"
#include "settings.h"
#include "ui_common.h"

namespace screens {
namespace {

const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);
platform::TransferMode g_mode = platform::TransferMode::Wifi;

std::string Size(uint64_t b) {
    char buf[32];
    if (b >= 1048576) std::snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%llu KB", (unsigned long long)std::max<uint64_t>(1, b / 1024));
    return buf;
}

lv_obj_t* Qr(lv_obj_t* parent, const std::string& data, int size) {
    lv_obj_t* qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, data.data(), (uint32_t)data.size());
    return qr;
}

lv_obj_t* Wrap(lv_obj_t* parent, const std::string& t, const lv_font_t* f) {
    lv_obj_t* l = ui::Label(parent, t, f);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

std::string WifiEsc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == ';' || c == ',' || c == ':' || c == '\\' || c == '"') o += '\\'; o += c; }
    return o;
}

struct ShareState {
    std::shared_ptr<bool> alive;
    lv_obj_t* log = nullptr;
    std::deque<std::string> lines;
    int sent = 0;
};

}  // namespace

void OpenShareBooks(const std::string& title, const std::vector<std::string>& paths) {
    std::vector<platform::ShareItem> items;
    for (const auto& p : paths) {
        if (!platform::FileExists(p)) continue;
        platform::ShareItem it;
        it.path = p;
        it.name = p.substr(p.rfind('/') + 1);
        it.size = platform::FileSize(p);
        items.push_back(it);
    }
    if (items.empty()) { ui::Toast("Không có sách nào để chia sẻ."); return; }
    auto* s = new ShareState();
    ui::Page p = ui::NewPage("CHIA SẺ SÁCH", [] { OpenLibrary(); });
    s->alive = ui::Alive(p.screen);
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) {
        platform::TransferStop();
        delete static_cast<ShareState*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, s);
    lv_obj_set_style_pad_row(p.body, 12, 0);

    // Mode chips
    lv_obj_t* modes = lv_obj_create(p.body);
    lv_obj_set_size(modes, LV_PCT(100), 72);
    lv_obj_set_style_border_width(modes, 0, 0);
    lv_obj_set_style_pad_all(modes, 0, 0);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(modes, 10, 0);
    lv_obj_remove_flag(modes, LV_OBJ_FLAG_SCROLLABLE);
    const int mw = (ui::kW - 2 * ui::kPad - 10) / 2;
    auto reopen = [title, paths](platform::TransferMode m) { g_mode = m; OpenShareBooks(title, paths); };
    lv_obj_t* m1 = ui::Button(modes, std::string(icon::WIFI) + "  Wi-Fi nhà", [reopen] { reopen(platform::TransferMode::Wifi); }, mw, 70);
    lv_obj_t* m2 = ui::Button(modes, std::string(icon::DEVICE) + "  Phát Wi-Fi", [reopen] { reopen(platform::TransferMode::Hotspot); }, mw, 70);
    lv_obj_set_style_radius(m1, 35, 0);
    lv_obj_set_style_radius(m2, 35, 0);
    ui::Invert(m1, g_mode == platform::TransferMode::Wifi);
    ui::Invert(m2, g_mode == platform::TransferMode::Hotspot);

    platform::TransferInfo info;
    std::string err;
    auto alive = s->alive;
    const bool hotspot = g_mode == platform::TransferMode::Hotspot;
    bool ok = (hotspot || settings::Get().wifi) && platform::ShareStart(g_mode, title, items, [s, alive](const platform::TransferEvent& e) {
        if (!*alive || e.action != "share_download") return;
        s->sent++;
        s->lines.push_front("Đã gửi: " + e.name + " (" + Size(e.bytes) + ")");
        if (s->lines.size() > 4) s->lines.pop_back();
        lv_obj_clean(s->log);
        for (auto& l : s->lines) {
            lv_obj_t* t = ui::Label(s->log, l, ui::F().small);
            lv_obj_set_size(t, LV_PCT(100), lv_font_get_line_height(ui::F().small));
            lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        }
        platform::Vibrate(settings::Get().haptic);
    }, info, err);
    uint64_t total = 0;
    for (auto& it : items) total += it.size;
    lv_label_set_text_fmt(p.header_status, "%d cuốn · %s · chỉ cho tải về", (int)items.size(), Size(total).c_str());
    if (!ok) {
        Wrap(p.body, !hotspot && !settings::Get().wifi ? "Wi-Fi đang tắt. Bật Wi-Fi, hoặc chọn \"Phát Wi-Fi\" để người nhận kết nối thẳng vào máy."
                                                        : "Không mở được chia sẻ: " + err, ui::F().body);
        ui::Show(p.screen);
        return;
    }

    lv_obj_t* card = lv_obj_create(p.body);
    lv_obj_set_size(card, LV_PCT(100), hotspot ? 330 : 360);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (hotspot) {
        lv_obj_align(Qr(card, "WIFI:T:WPA;S:" + WifiEsc(info.ssid) + ";P:" + WifiEsc(info.password) + ";;", 180), LV_ALIGN_TOP_LEFT, 40, 0);
        lv_obj_align(Qr(card, info.url, 180), LV_ALIGN_TOP_RIGHT, -40, 0);
        lv_obj_align(ui::Label(card, "1 · Vào Wi-Fi của máy", ui::F().small), LV_ALIGN_BOTTOM_LEFT, 20, -42);
        lv_obj_align(ui::Label(card, "2 · Mở trang tải sách", ui::F().small), LV_ALIGN_BOTTOM_RIGHT, -20, -42);
        lv_obj_align(ui::Label(card, info.ssid + "  ·  mật khẩu " + info.password, ui::F().body_bold), LV_ALIGN_BOTTOM_MID, 0, 4);
    } else {
        lv_obj_align(Qr(card, info.url, 300), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* h = ui::Label(card, "Người nhận quét mã để tải sách về điện thoại hoặc máy tính.", ui::F().small);
        lv_obj_set_width(h, 270);
        lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_align(h, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_t* u = ui::Label(card, info.url, ui::F().small);
        lv_obj_set_width(u, 270);
        lv_label_set_long_mode(u, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_align(u, LV_ALIGN_TOP_RIGHT, 0, 120);
    }
    if (hotspot) Wrap(p.body, "Địa chỉ: " + info.url, ui::F().small);
    Wrap(p.body, std::string(icon::DEVICE) + "  Máy Metalio khác: Ứng dụng › Thư viện online › Nhận sách, nhập địa chỉ trên.", ui::F().small);

    lv_obj_t* h = ui::Label(p.body, "ĐANG CHIA SẺ", ui::F().small);
    lv_obj_set_style_text_letter_space(h, 3, 0);
    lv_obj_set_style_text_color(h, kMuted, 0);
    const size_t show = std::min<size_t>(items.size(), 4);
    for (size_t i = 0; i < show; ++i) {
        const auto& m = library::LoadMeta(items[i].path);
        lv_obj_t* row = lv_obj_create(p.body);
        lv_obj_set_size(row, LV_PCT(100), 54);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, kHair, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_t* t = ui::Label(row, std::string(icon::BOOK) + "  " + m.title, ui::F().body);
        lv_obj_set_width(t, 480);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(ui::Label(row, Size(items[i].size), ui::F().small), LV_ALIGN_RIGHT_MID, 0, 0);
    }
    if (items.size() > show) Wrap(p.body, "… và " + std::to_string(items.size() - show) + " cuốn khác", ui::F().small);
    s->log = lv_obj_create(p.body);
    lv_obj_set_size(s->log, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(s->log, 0, 0);
    lv_obj_set_style_pad_all(s->log, 0, 0);
    lv_obj_set_flex_flow(s->log, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s->log, LV_OBJ_FLAG_SCROLLABLE);
    ui::Label(s->log, "Chưa ai tải.", ui::F().small);
    lv_obj_t* stop = ui::Button(p.body, "Dừng chia sẻ", [] { OpenLibrary(); }, LV_PCT(100), 76);
    ui::Invert(stop, true);
    ui::Show(p.screen);
}

void OpenShareLink(const std::string& name, const std::string& link) {
    ui::Page p = ui::NewPage("CHIA SẺ THƯ VIỆN", [] { OpenOnlineLibraries(); });
    lv_label_set_text(p.header_status, name.c_str());
    lv_obj_set_style_pad_row(p.body, 16, 0);
    lv_obj_t* card = lv_obj_create(p.body);
    lv_obj_set_size(card, LV_PCT(100), 440);
    lv_obj_set_style_border_width(card, 3, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(Qr(card, link, 380));
    Wrap(p.body, "Quét mã để mở thư viện \"" + name + "\" trên điện thoại. Trên một máy Metalio khác: Thư viện online › Thêm thư viện, dán link này (hoặc dán trong trang Chuyển file).", ui::F().body);
    lv_obj_t* l = Wrap(p.body, link, ui::F().small);
    lv_obj_set_style_text_color(l, kMuted, 0);
    Wrap(p.body, "Thư mục Drive phải đang chia sẻ \"Bất kỳ ai có đường liên kết\" thì người khác mới mở được.", ui::F().small);
    ui::Show(p.screen);
}

namespace {

struct Recv {
    std::string base;   // http://host:port/s/token/
    std::string title;
    struct Item { std::string name, path; uint64_t size; bool done = false; };
    std::vector<Item> items;
    uint32_t req = 0, dl = 0;
    int downloading = -1;
    lv_obj_t* body = nullptr;
    lv_obj_t* status = nullptr;
    std::shared_ptr<bool> alive;
};

void RenderRecv(Recv* r);

// "http://1.2.3.4:8088/s/abcd" → "http://1.2.3.4:8088/s/abcd/" ; only http(s) share addresses.
std::string NormalizeShareUrl(std::string u) {
    while (!u.empty() && (u.back() == ' ' || u.back() == '\n')) u.pop_back();
    while (!u.empty() && u.front() == ' ') u.erase(0, 1);
    if (u.compare(0, 7, "http://") != 0 && u.compare(0, 8, "https://") != 0) u = "http://" + u;
    size_t q = u.find('?');
    if (q != std::string::npos) u.resize(q);
    if (u.size() > 9 && u.compare(u.size() - 9, 9, "list.json") == 0) u.resize(u.size() - 9);
    if (u.back() != '/') u += '/';
    if (u.find("/s/") == std::string::npos) return "";
    return u;
}

void DownloadNext(Recv* r) {
    if (r->dl) return;
    for (size_t i = 0; i < r->items.size(); ++i) {
        if (r->items[i].done || (int)i != r->downloading) continue;
        const auto& it = r->items[i];
        const std::string dir = drive::IsAudio(it.name) ? platform::MusicDir() : platform::GetConfig().books_dir;
        const std::string path = dir + "/" + drive::SafeFileName(it.name);
        auto alive = r->alive;
        r->dl = platform::Download(r->base + it.path, path, [r, alive](uint64_t done, uint64_t total) {
            if (!*alive || !total) return;
            lv_label_set_text_fmt(r->status, "Đang tải %d%%…", (int)(done * 100 / total));
        }, [r, alive, i](const platform::HttpResponse& resp) {
            if (!*alive) return;
            r->dl = 0;
            r->downloading = -1;
            if (resp.ok()) { r->items[i].done = true; library::Invalidate(); ui::Toast("Đã nhận: " + r->items[i].name, 1500); }
            else ui::Toast("Tải lỗi: " + (resp.error.empty() ? "HTTP " + std::to_string(resp.status) : resp.error));
            RenderRecv(r);
        });
        return;
    }
}

void RenderRecv(Recv* r) {
    lv_obj_clean(r->body);
    if (r->items.empty()) {
        Wrap(r->body, r->req ? "Đang kết nối tới máy chia sẻ…" : "Không có sách nào.", ui::F().body);
        return;
    }
    lv_label_set_text_fmt(r->status, "%s · %d tệp", r->title.c_str(), (int)r->items.size());
    for (size_t i = 0; i < r->items.size() && i < 8; ++i) {
        const auto& it = r->items[i];
        lv_obj_t* row = lv_obj_create(r->body);
        lv_obj_set_size(row, LV_PCT(100), 96);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, kHair, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = ui::Label(row, it.name, ui::F().body_bold);
        lv_obj_set_width(t, 420);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 12);
        lv_obj_align(ui::Label(row, Size(it.size), ui::F().small), LV_ALIGN_BOTTOM_LEFT, 0, -12);
        const bool busy = (int)i == r->downloading;
        lv_obj_t* b = ui::Button(row, it.done ? std::string(icon::CHECK) + " Đã nhận" : busy ? "Đang tải…" : std::string(icon::DOWNLOAD) + " Nhận",
                                 [r, i] { if (r->downloading < 0 && !r->items[i].done) { r->downloading = (int)i; RenderRecv(r); DownloadNext(r); } }, 170, 64);
        ui::Invert(b, it.done);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    ui::Button(r->body, "Nhận tất cả", [r] {
        for (size_t i = 0; i < r->items.size(); ++i) if (!r->items[i].done) { r->downloading = (int)i; break; }
        RenderRecv(r);
        DownloadNext(r);
    }, LV_PCT(100), 76);
}

void Connect(const std::string& raw) {
    std::string base = NormalizeShareUrl(raw);
    if (base.empty()) { ui::Toast("Địa chỉ chia sẻ có dạng http://…/s/…"); return; }
    auto* r = new Recv();
    r->base = base;
    ui::Page p = ui::NewPage("NHẬN SÁCH", [] { OpenOnlineLibraries(); });
    r->alive = ui::Alive(p.screen);
    r->body = p.body;
    r->status = p.header_status;
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) {
        auto* x = static_cast<Recv*>(lv_event_get_user_data(e));
        if (x->req) platform::Cancel(x->req);
        if (x->dl) platform::Cancel(x->dl);
        delete x;
    }, LV_EVENT_DELETE, r);
    lv_label_set_text(p.header_status, base.c_str());
    platform::HttpRequest req;
    req.url = base + "list.json";
    auto alive = r->alive;
    r->req = platform::Http(req, [r, alive](const platform::HttpResponse& resp) {
        if (!*alive) return;
        r->req = 0;
        cJSON* o = resp.ok() ? cJSON_Parse(resp.body.c_str()) : nullptr;
        const cJSON* t = cJSON_GetObjectItem(o, "title");
        r->title = cJSON_IsString(t) ? t->valuestring : "Sách được chia sẻ";
        const cJSON* it;
        cJSON_ArrayForEach(it, cJSON_GetObjectItem(o, "items")) {
            const cJSON* n = cJSON_GetObjectItem(it, "name");
            const cJSON* pth = cJSON_GetObjectItem(it, "path");
            const cJSON* sz = cJSON_GetObjectItem(it, "size");
            if (!cJSON_IsString(n) || !cJSON_IsString(pth)) continue;
            std::string path = pth->valuestring;
            // only "f/<n>" paths on the same address: the list cannot send us elsewhere
            if (path.compare(0, 2, "f/") != 0 || path.find_first_not_of("0123456789", 2) != std::string::npos) continue;
            if (!drive::IsReadable(n->valuestring) && !drive::IsAudio(n->valuestring)) continue;
            r->items.push_back({n->valuestring, path, cJSON_IsNumber(sz) ? (uint64_t)sz->valuedouble : 0});
        }
        cJSON_Delete(o);
        if (!resp.ok()) {
            lv_obj_clean(r->body);
            Wrap(r->body, "Không kết nối được: " + (resp.error.empty() ? "HTTP " + std::to_string(resp.status) : resp.error) +
                          ". Hai máy phải cùng Wi-Fi (hoặc máy này vào Wi-Fi do máy kia phát), và máy kia đang mở màn Chia sẻ.", ui::F().body);
            return;
        }
        RenderRecv(r);
    });
    RenderRecv(r);
    ui::Show(p.screen);
}

}  // namespace

void OpenReceive() {
    ui::PromptOptions o;
    o.title = "Nhận sách";
    o.hint = "Nhập địa chỉ đang hiện trên máy chia sẻ sách.\nVí dụ:  192.168.1.20:8088/s/abcd2345";
    o.placeholder = "192.168.…/s/…";
    o.vietnamese = false;
    o.ok = "Kết nối";
    ui::TextPrompt(o, [](const std::string& u) { if (!u.empty()) Connect(u); });
}

}  // namespace screens
