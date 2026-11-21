// "Chuyển file": starts the Wi-Fi transfer server, shows its address + QR code and a live log of
// what arrives. Books go to the shelf, music to the player. Stops when the screen closes.
#include <cstdio>
#include <deque>
#include <cstdlib>
#include <memory>

#include "ai_client.h"
#include "dictionary.h"
#include "icons.h"
#include "library.h"
#include "online_library.h"
#include "platform.h"
#include "screens.h"
#include "settings.h"
#include "ui_common.h"

namespace screens {
namespace {

struct State {
    std::shared_ptr<bool> alive;
    lv_obj_t* log = nullptr;
    std::deque<std::string> lines;
    int received = 0;
    lv_obj_t* count = nullptr;
};

std::string Size(uint64_t b) {
    char buf[32];
    if (b >= 1048576) std::snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%llu KB", (unsigned long long)std::max<uint64_t>(1, b / 1024));
    return buf;
}

void Step(lv_obj_t* parent, const char* n, const std::string& text) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* dot = lv_obj_create(row);
    lv_obj_remove_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(dot, 40, 40);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_black(), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_t* num = ui::Label(dot, n, ui::F().body_bold);
    lv_obj_set_style_text_color(num, lv_color_white(), 0);
    lv_obj_center(num);
    lv_obj_t* t = ui::Label(row, text, ui::F().body);
    lv_obj_set_width(t, ui::kW - 2 * ui::kPad - 60);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(t, 56, 4);
}

void RenderLog(State* s) {
    lv_obj_clean(s->log);
    if (s->lines.empty()) {
        ui::Label(s->log, "Chưa nhận tệp nào.", ui::F().small);
        return;
    }
    for (const auto& l : s->lines) {
        lv_obj_t* t = ui::Label(s->log, l, ui::F().small);
        lv_obj_set_size(t, LV_PCT(100), lv_font_get_line_height(ui::F().small));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    }
}

}  // namespace

namespace {
platform::TransferMode g_mode = platform::TransferMode::Wifi;

lv_obj_t* Qr(lv_obj_t* parent, const std::string& data, int size) {
    lv_obj_t* qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, data.data(), (uint32_t)data.size());
    return qr;
}

std::string WifiQrEscape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == ';' || c == ',' || c == ':' || c == '\\' || c == '"') o += '\\'; o += c; }
    return o;
}
}  // namespace

namespace { std::string g_focus; }   // "ai": opened from Cài đặt › Trợ lý AI

void OpenTransferFor(const std::string& focus) {
    g_focus = focus;
    OpenTransfer();
}

void OpenTransfer() {
    const std::string focus = g_focus;
    g_focus.clear();
    auto* s = new State();
    ui::Page p = ui::NewPage(focus == "ai" ? "CÀI TRỢ LÝ AI QUA WI-FI" : "CHUYỂN FILE",
                             [focus] { if (focus == "ai") OpenAssistantSettings(); else OpenApps(); });
    s->alive = ui::Alive(p.screen);
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) {
        platform::TransferStop();
        delete static_cast<State*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, s);
    lv_obj_set_style_pad_row(p.body, 14, 0);

    // Mode: home Wi-Fi, or the device's own hotspot (no router needed: phone joins the reader).
    lv_obj_t* modes = lv_obj_create(p.body);
    lv_obj_set_size(modes, LV_PCT(100), 72);
    lv_obj_set_style_border_width(modes, 0, 0);
    lv_obj_set_style_pad_all(modes, 0, 0);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(modes, 10, 0);
    lv_obj_remove_flag(modes, LV_OBJ_FLAG_SCROLLABLE);
    const int mw = (ui::kW - 2 * ui::kPad - 10) / 2;
    lv_obj_t* m1 = ui::Button(modes, std::string(icon::WIFI) + "  Wi-Fi nhà", [focus] { g_mode = platform::TransferMode::Wifi; OpenTransferFor(focus); }, mw, 70);
    lv_obj_t* m2 = ui::Button(modes, std::string(icon::DEVICE) + "  Phát Wi-Fi", [focus] { g_mode = platform::TransferMode::Hotspot; OpenTransferFor(focus); }, mw, 70);
    lv_obj_set_style_radius(m1, 35, 0);
    lv_obj_set_style_radius(m2, 35, 0);
    ui::Invert(m1, g_mode == platform::TransferMode::Wifi);
    ui::Invert(m2, g_mode == platform::TransferMode::Hotspot);

    platform::TransferInfo info;
    std::string err;
    auto alive = s->alive;
    const bool hotspot = g_mode == platform::TransferMode::Hotspot;
    bool ok = (hotspot || settings::Get().wifi) && platform::TransferStart(g_mode, [s, alive](const platform::TransferEvent& e) {
        if (!*alive) return;
        static const std::pair<const char*, const char*> names[] = {{"music", "nhạc"}, {"books", "sách"}, {"fonts", "phông chữ"},
                                                                    {"dict", "từ điển"}, {"vocab", "bộ từ"}, {"notes", "ghi chú"}};
        std::string what = e.dir;
        for (auto& n : names) if (e.dir == n.first) what = n.second;
        std::string line;
        if (e.action == "library_add") {
            std::string err2;
            line = online::Add(e.name, e.detail, err2) ? "Đã thêm thư viện online: " + (e.name.empty() ? std::string("(không tên)") : e.name)
                                                        : "Không thêm được thư viện: " + err2;
        } else if (e.action == "ai_config") {
            ai::Config c = ai::Load();
            for (int i = 0; i < (int)ai::Provider::Count; ++i)
                if (e.dir == ai::Info((ai::Provider)i).key) c.provider = (ai::Provider)i;
            const size_t nl = e.detail.find('\n');
            const std::string key = e.detail.substr(0, nl), url = nl == std::string::npos ? "" : e.detail.substr(nl + 1);
            if (!key.empty()) c.api_key = key;
            c.model = e.name;
            if (!url.empty()) c.base_url = url;
            ai::Save(c);
            line = std::string("Đã lưu trợ lý AI: ") + ai::ProviderName(c.provider) + " · khoá " + ai::MaskKey(c.api_key);
        } else if (e.action == "library_delete") {
            online::Remove((size_t)std::atoi(e.name.c_str()));
            line = "Đã xoá một thư viện online";
        } else if (e.action == "upload") {
            line = e.ok ? "Đã nhận " + what + ": " + e.name + " (" + Size(e.bytes) + ")" : "Lỗi khi nhận " + e.name + ": " + e.detail;
            if (e.ok && e.dir == "dict") dict::Reload();
            if (e.ok && e.dir == "books") library::Invalidate();
        } else if (e.action == "delete") {
            line = "Đã xoá " + e.name;
            library::Invalidate();
        } else {
            line = e.detail;
        }
        s->lines.push_front(line);
        if (s->lines.size() > 6) s->lines.pop_back();
        if (e.action == "upload" && e.ok) {
            s->received++;
            lv_label_set_text_fmt(s->count, "Đã nhận %d tệp trong lần này", s->received);
            platform::Vibrate(settings::Get().haptic);
        }
        RenderLog(s);
    }, info, err);
    const std::string page_url = info.url + (focus == "ai" ? "#ai" : "");

    if (!ok) {
        lv_label_set_text(p.header_status, "Chưa sẵn sàng");
        lv_obj_t* l = ui::Label(p.body, !hotspot && !settings::Get().wifi ? "Wi-Fi đang tắt. Bật Wi-Fi trong Cài đặt, hoặc chọn \"Phát Wi-Fi\" để điện thoại kết nối thẳng vào máy."
                                                                           : "Không mở được chuyển file: " + err, ui::F().body);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        if (!hotspot) ui::Button(p.body, "Mở cài đặt Wi-Fi", [] { OpenSettings(); }, LV_PCT(100));
    } else {
        lv_label_set_text(p.header_status, hotspot ? ("Đang phát Wi-Fi · " + info.ssid).c_str() : ("Đang mở · " + platform::WifiName()).c_str());
        lv_obj_t* card = lv_obj_create(p.body);
        lv_obj_set_size(card, LV_PCT(100), hotspot ? 320 : 330);
        lv_obj_set_style_border_width(card, 3, 0);
        lv_obj_set_style_border_color(card, lv_color_black(), 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 18, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        if (hotspot) {
            // Two codes: 1) join the reader's Wi-Fi (camera apps understand WIFI: codes), 2) open the page.
            lv_obj_t* q1 = Qr(card, "WIFI:T:WPA;S:" + WifiQrEscape(info.ssid) + ";P:" + WifiQrEscape(info.password) + ";;", 180);
            lv_obj_align(q1, LV_ALIGN_TOP_LEFT, 40, 0);
            lv_obj_t* q2 = Qr(card, page_url, 180);
            lv_obj_align(q2, LV_ALIGN_TOP_RIGHT, -40, 0);
            lv_obj_t* c1 = ui::Label(card, "1 · Quét để vào Wi-Fi", ui::F().small);
            lv_obj_align(c1, LV_ALIGN_BOTTOM_LEFT, 30, -40);
            lv_obj_t* c2 = ui::Label(card, "2 · Quét để mở trang", ui::F().small);
            lv_obj_align(c2, LV_ALIGN_BOTTOM_RIGHT, -30, -40);
            lv_obj_t* cred = ui::Label(card, info.ssid + "  ·  mật khẩu " + info.password, ui::F().body_bold);
            lv_obj_align(cred, LV_ALIGN_BOTTOM_MID, 0, 4);
            s->count = ui::Label(p.body, "Đang chờ tệp…  ·  " + info.url, ui::F().small);
        } else {
            lv_obj_t* qr = Qr(card, page_url, 280);
            lv_obj_align(qr, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_t* hint = ui::Label(card, "Quét mã bằng điện thoại, hoặc mở trên máy tính:", ui::F().small);
            lv_obj_set_width(hint, 300);
            lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, 0, 0);
            lv_obj_t* u = ui::Label(card, info.url, ui::F().body_bold);
            lv_obj_set_width(u, 300);
            lv_label_set_long_mode(u, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_align(u, LV_ALIGN_TOP_RIGHT, 0, 72);
            s->count = ui::Label(card, "Đang chờ tệp…", ui::F().small);
            lv_obj_set_width(s->count, 300);
            lv_label_set_long_mode(s->count, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_align(s->count, LV_ALIGN_TOP_RIGHT, 0, 180);
        }

        // What can be sent, and where it goes.
        lv_obj_t* kinds = lv_obj_create(p.body);
        lv_obj_set_size(kinds, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(kinds, 0, 0);
        lv_obj_set_style_pad_all(kinds, 0, 0);
        lv_obj_set_style_pad_gap(kinds, 8, 0);
        lv_obj_set_flex_flow(kinds, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(kinds, LV_OBJ_FLAG_SCROLLABLE);
        const std::pair<const char*, const char*> ks[] = {{icon::BOOK, "Sách EPUB, TXT"}, {icon::MUSIC, "Nhạc MP3, WAV"}, {icon::FONT, "Phông TTF, OTF"},
                                                          {icon::DICT, "Từ điển StarDict"}, {icon::VOCAB, "Bộ từ vựng .tsv"}, {icon::NOTES, "Tải ghi chú về"},
                                                          {icon::DOWNLOAD, "Thư viện online (link Drive)"}};
        for (auto& k : ks) {
            lv_obj_t* chip = lv_obj_create(kinds);
            lv_obj_set_size(chip, LV_SIZE_CONTENT, 56);
            lv_obj_set_style_radius(chip, 28, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_border_color(chip, lv_color_hex(0xb9b6ae), 0);
            lv_obj_set_style_pad_hor(chip, 16, 0);
            lv_obj_remove_flag(chip, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_center(ui::Label(chip, std::string(k.first) + " " + k.second, ui::F().small));
        }
        if (focus == "ai") {
            Step(p.body, "1", hotspot ? "Điện thoại vào Wi-Fi \"" + info.ssid + "\" của máy, rồi quét mã 2." : "Điện thoại hoặc máy tính cùng Wi-Fi \"" + platform::WifiName() + "\": quét mã hoặc mở địa chỉ trên.");
            Step(p.body, "2", "Ở mục Trợ lý AI: chọn nhà cung cấp, dán khoá API, bấm \"Lưu vào máy\". Khoá không hiện lại đầy đủ trên trang.");
        } else if (hotspot) {
            Step(p.body, "1", "Trên điện thoại: vào Wi-Fi \"" + info.ssid + "\" (quét mã 1 hoặc nhập mật khẩu).");
            Step(p.body, "2", "Mở trang (quét mã 2). Chọn tệp, hoặc dán link Google Drive ở mục Thư viện online.");
        } else {
            Step(p.body, "1", "Điện thoại hoặc máy tính dùng cùng Wi-Fi \"" + platform::WifiName() + "\", mở địa chỉ trên.");
            Step(p.body, "2", "Chọn tệp: máy tự xếp vào books/, music/, fonts/, dict/, vocab/. Dán link Drive để thêm thư viện online.");
        }
        ui::Label(p.body, "Nhật ký", ui::F().body_bold);
        s->log = lv_obj_create(p.body);
        lv_obj_set_size(s->log, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(s->log, 0, 0);
        lv_obj_set_style_pad_all(s->log, 0, 0);
        lv_obj_set_flex_flow(s->log, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(s->log, 4, 0);
        RenderLog(s);
    }
    ui::Button(p.body, "Xong · dừng chuyển file", [] { OpenLauncher(); }, LV_PCT(100), 76);
    ui::Show(p.screen);
}

}  // namespace screens
