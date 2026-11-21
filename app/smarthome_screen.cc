// "Nhà thông minh" screen: reads the ZBOX kiosk bridge (same API as Kreader's HomeCenter).
#include <cstdio>
#include <ctime>
#include <memory>

#include "home_gate.h"
#include "kiosk_model.h"
#include "platform.h"
#include "screens.h"
#include "ui_common.h"

namespace screens {
namespace {

constexpr uint32_t kRefreshMs = 30000;  // e-ink: refresh rarely; each redraw costs a panel update

struct State {
    ui::Page page;
    std::shared_ptr<bool> alive;
    home::Summary data;
    bool loaded = false;
    int room = 0;
    lv_obj_t* env_row = nullptr;
    lv_obj_t* quick = nullptr;
    lv_obj_t* tabs = nullptr;
    lv_obj_t* room_list = nullptr;
    lv_timer_t* timer = nullptr;
    uint32_t pending = 0;
};

void Fetch(State* s);
void Render(State* s);

std::string Now() {
    std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M", std::localtime(&t));
    return buf;
}

void SetStatus(State* s, const std::string& text) { lv_label_set_text(s->page.header_status, text.c_str()); }

void SendCommand(State* s, const home::Command& cmd, const std::string& what) {
    const auto& cfg = platform::GetConfig();
    if (!cfg.allow_control) {
        ui::Toast("Chế độ CHỈ ĐỌC: chưa gửi lệnh \"" + what + "\".\nChạy giả lập với --allow-control để bật điều khiển.");
        return;
    }
    platform::HttpRequest req;
    req.method = "POST";
    req.url = cfg.kiosk_base_url + "/api/ha/call";
    req.body = home::CommandJson(cmd);
    req.content_type = "application/json";
    req.token = cfg.kiosk_token;
    platform::Log("[home] POST %s %s", req.url.c_str(), req.body.c_str());
    ui::Toast("Đang gửi: " + what + "…", 1500);
    auto alive = s->alive;
    platform::Http(req, [s, alive, what](const platform::HttpResponse& r) {
        if (!*alive) return;
        if (!r.ok()) {
            ui::Toast("Lỗi gửi lệnh (" + (r.status ? std::to_string(r.status) : r.error) + ")");
            return;
        }
        ui::Toast("Đã gửi: " + what, 1500);
        // HA needs a moment to report the new state.
        lv_timer_t* t = lv_timer_create([](lv_timer_t* tm) {
            auto* st = static_cast<State*>(lv_timer_get_user_data(tm));
            Fetch(st);
        }, 1200, s);
        lv_timer_set_repeat_count(t, 1);
    });
}

lv_obj_t* DeviceCard(State* s, lv_obj_t* parent, const home::Device& d, int width) {
    lv_obj_t* card = ui::Button(parent, "", [s, d] {
        SendCommand(s, home::ToggleCommand(d), std::string(d.on ? "Tắt " : "Bật ") + d.name);
    }, width, d.type == home::DeviceType::Ac ? 222 : 146);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // One line each: DOTS only truncates when the label height is fixed.
    auto one_line = [card](const std::string& text, const lv_font_t* font) {
        lv_obj_t* l = ui::Label(card, text, font);
        lv_obj_set_size(l, LV_PCT(100), lv_font_get_line_height(font));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        return l;
    };
    one_line(d.name, ui::F().body_bold);
    one_line(d.room.empty() ? d.sub : d.room + " · " + d.sub, ui::F().small);
    one_line(std::string(home::TypeLabel(d.type)) + ": " + home::StateLabel(d), ui::F().body_bold);
    ui::Invert(card, d.on);

    if (d.type == home::DeviceType::Ac) {
        lv_obj_t* row = lv_obj_create(card);
        lv_obj_set_size(row, LV_PCT(100), 66);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        for (int delta : {-1, +1}) {
            lv_obj_t* b = ui::Button(row, delta < 0 ? "- 1°C" : "+ 1°C", [s, d, delta] {
                int target = d.temp + delta;
                if (target < 16 || target > 30) { ui::Toast("Nhiệt độ chỉ trong khoảng 16–30°C"); return; }
                SendCommand(s, home::AcTempCommand(d, target), d.name + " › " + std::to_string(target) + "°C");
            }, 130, 62);
            (void)b;
        }
    }
    return card;
}

lv_obj_t* Section(lv_obj_t* parent, const std::string& title) {
    ui::Label(parent, title, ui::F().body_bold);
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_row(box, 12, 0);
    lv_obj_set_style_pad_column(box, 12, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

lv_obj_t* EnvTile(lv_obj_t* parent, const std::string& label, const std::string& value, int w) {
    lv_obj_t* t = lv_obj_create(parent);
    lv_obj_set_size(t, w, 130);
    lv_obj_set_style_border_width(t, 3, 0);
    lv_obj_set_style_border_color(t, lv_color_black(), 0);
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_set_style_pad_all(t, 12, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = ui::Label(t, label, ui::F().small);
    lv_obj_set_size(l, w - 30, lv_font_get_line_height(ui::F().small));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t* v = ui::Label(t, value, ui::F().title);
    lv_obj_set_size(v, w - 30, lv_font_get_line_height(ui::F().title));
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return t;
}

void RenderRoom(State* s) {
    lv_obj_clean(s->room_list);
    if (s->data.rooms.empty()) return;
    if (s->room >= (int)s->data.rooms.size()) s->room = 0;
    const int w2 = (ui::kW - 2 * ui::kPad - 12) / 2;
    for (const auto& d : s->data.rooms[s->room].devices) DeviceCard(s, s->room_list, d, w2);
    // Reflect selection on the tabs (inverted = selected).
    uint32_t n = lv_obj_get_child_count(s->tabs);
    for (uint32_t i = 0; i < n; ++i) ui::Invert(lv_obj_get_child(s->tabs, i), (int)i == s->room);
}

void Render(State* s) {
    lv_obj_t* body = s->page.body;
    lv_obj_clean(body);
    const int inner = ui::kW - 2 * ui::kPad;
    const auto& d = s->data;

    lv_obj_t* env = Section(body, "Môi trường");
    char a[48], b[48], c[64];
    std::snprintf(a, sizeof a, "%.1f°C", d.indoor_temp);
    std::snprintf(b, sizeof b, "%.0f%%", d.indoor_humid);
    std::snprintf(c, sizeof c, "%.0f°C", d.outdoor_temp);
    int w3 = (inner - 24) / 3;
    EnvTile(env, "Trong nhà", a, w3);
    EnvTile(env, "Độ ẩm", b, w3);
    EnvTile(env, "Ngoài: " + d.outdoor_desc, c, w3);

    s->quick = Section(body, "Thiết bị nhanh");
    const int w2 = (inner - 12) / 2;
    for (const auto& dev : d.quick) DeviceCard(s, s->quick, dev, w2);

    ui::Label(body, "Theo phòng", ui::F().body_bold);
    s->tabs = lv_obj_create(body);
    lv_obj_set_size(s->tabs, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s->tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s->tabs, 0, 0);
    lv_obj_set_style_pad_all(s->tabs, 0, 0);
    lv_obj_set_style_pad_gap(s->tabs, 10, 0);
    lv_obj_set_flex_flow(s->tabs, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(s->tabs, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < (int)d.rooms.size(); ++i) {
        ui::Button(s->tabs, d.rooms[i].title, [s, i] { s->room = i; RenderRoom(s); }, LV_SIZE_CONTENT, 72);
    }
    s->room_list = lv_obj_create(body);
    lv_obj_set_size(s->room_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s->room_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s->room_list, 0, 0);
    lv_obj_set_style_pad_all(s->room_list, 0, 0);
    lv_obj_set_style_pad_gap(s->room_list, 12, 0);
    lv_obj_set_flex_flow(s->room_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(s->room_list, LV_OBJ_FLAG_SCROLLABLE);
    RenderRoom(s);

    ui::Button(body, "Làm mới", [s] { Fetch(s); }, LV_PCT(100));
}

void RenderAway(State* s, const std::string& why) {
    lv_obj_t* body = s->page.body;
    lv_obj_clean(body);
    lv_obj_t* l = ui::Label(body, "Không kết nối được ZBOX ở nhà.\n\n" + why +
                                      "\n\nMenu này chỉ dùng được khi máy đang bắt Wi-Fi nhà.",
                            ui::F().body);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    ui::Button(body, "Thử lại", [s] { Fetch(s); }, LV_PCT(100));
}

void Fetch(State* s) {
    const auto& cfg = platform::GetConfig();
    std::string host = home::UrlHost(cfg.kiosk_base_url);
    // Simulator runs on the ZBOX itself, so loopback counts as "home" there only.
    if (!home::IsPrivateIpv4(host, /*allow_loopback=*/true)) {
        RenderAway(s, "Địa chỉ ZBOX \"" + host + "\" không phải IP mạng nội bộ.");
        return;
    }
    if (s->pending) return;
    SetStatus(s, "Đang kết nối ZBOX…");
    platform::HttpRequest req;
    req.url = cfg.kiosk_base_url + "/api/kiosk/summary";
    req.token = cfg.kiosk_token;
    req.timeout_ms = 4000;
    auto alive = s->alive;
    s->pending = platform::Http(req, [s, alive](const platform::HttpResponse& r) {
        if (!*alive) return;
        s->pending = 0;
        if (!r.ok()) {
            std::string why = r.status ? "HTTP " + std::to_string(r.status) : r.error;
            SetStatus(s, "Không ở nhà / ZBOX không phản hồi");
            if (!s->loaded) RenderAway(s, why);
            else ui::Toast("Làm mới thất bại: " + why);
            return;
        }
        home::Summary sum;
        std::string err;
        if (!home::ParseSummary(r.body, sum, err)) {
            SetStatus(s, "Dữ liệu ZBOX lỗi");
            RenderAway(s, err);
            return;
        }
        s->data = std::move(sum);
        s->loaded = true;
        SetStatus(s, "Ở nhà · cập nhật " + Now() +
                         (platform::GetConfig().allow_control ? " · ĐIỀU KHIỂN BẬT" : " · chỉ đọc"));
        Render(s);
    });
}

}  // namespace

void OpenSmartHome() {
    auto* s = new State();
    s->page = ui::NewPage("NHÀ THÔNG MINH", [] { OpenApps(); });
    s->alive = ui::Alive(s->page.screen);
    s->timer = lv_timer_create([](lv_timer_t* t) { Fetch(static_cast<State*>(lv_timer_get_user_data(t))); },
                               kRefreshMs, s);
    lv_obj_add_event_cb(s->page.screen, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        lv_timer_delete(st->timer);
        if (st->pending) platform::Cancel(st->pending);
        delete st;
    }, LV_EVENT_DELETE, s);
    ui::Label(s->page.body, "Đang tải…", ui::F().body);
    ui::Show(s->page.screen);
    Fetch(s);
}

}  // namespace screens
