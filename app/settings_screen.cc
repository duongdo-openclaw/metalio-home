// "Cài đặt": Kindle-style list of sections, each with its own page of e-ink friendly controls
// (radio rows, on/off rows, -/+ steppers). Mirrors the stock Metalio settings tabs.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "ai_client.h"
#include "library.h"
#include "owner.h"
#include "reading_state.h"
#include "platform.h"
#include "screens.h"
#include "settings.h"
#include "icons.h"
#include "ui_common.h"

namespace screens {
namespace {

enum class Section { Light, Screen, Font, Battery, Assistant, Keys, Haptic, Power, Wifi, Bluetooth, Storage, Language, Owner, About, Goals };
void OpenSection(Section s);
void OpenFormat(bool keep = true);

std::string Size(uint64_t b) {
    char buf[32];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.1f GB", b / 1073741824.0);
    else std::snprintf(buf, sizeof buf, "%.0f MB", b / 1048576.0);
    return buf;
}

void Update(const std::function<void(settings::Values&)>& change, Section reopen) {
    settings::Values v = settings::Get();
    change(v);
    const bool font_changed = v.ui_font != settings::Get().ui_font;
    settings::Set(v);
    if (font_changed) ui::ApplyUiFont(v.ui_font);
    platform::Vibrate(v.haptic);
    OpenSection(reopen);
}

lv_obj_t* Heading(lv_obj_t* parent, const std::string& title, const std::string& hint = "") {
    lv_obj_t* t = ui::Label(parent, title, ui::F().body_bold);
    lv_obj_set_style_pad_top(t, 8, 0);
    if (!hint.empty()) {
        lv_obj_t* h = ui::Label(parent, hint, ui::F().small);
        lv_obj_set_width(h, LV_PCT(100));
        lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
    }
    return t;
}

// Radio list: the selected option is inverted (black), like the stock settings.
void Choice(lv_obj_t* parent, const std::vector<std::string>& options, int selected, std::function<void(int)> pick) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_gap(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < (int)options.size(); ++i) {
        lv_obj_t* b = ui::Button(box, options[i], [pick, i] { pick(i); }, LV_SIZE_CONTENT, 68);
        lv_obj_set_style_radius(b, 34, 0);           // pill chips, same as quick settings
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_pad_hor(b, 24, 0);
        ui::Invert(b, i == selected);
    }
}

void Toggle(lv_obj_t* parent, const std::string& title, const std::string& hint, bool on, std::function<void()> flip) {
    lv_obj_t* row = ui::Button(parent, "", flip, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(row, 20, 0);
    lv_obj_set_style_pad_ver(row, 16, 0);
    lv_obj_set_style_min_height(row, 88, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_border_width(row, 2, 0);
    // Text column on the left (title + hint wrapping onto a second line), switch on the right.
    lv_obj_t* col = lv_obj_create(row);
    lv_obj_remove_flag(col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(col, ui::kW - 2 * ui::kPad - 40 - 168, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* t = ui::Label(col, title, ui::F().body_bold);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    if (!hint.empty()) {
        lv_obj_t* h = ui::Label(col, hint, ui::F().small);
        lv_obj_set_style_text_color(h, lv_color_hex(0x5f5d58), 0);
        lv_obj_set_width(h, LV_PCT(100));
        lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
    }
    // switch: pill with a knob; black = on
    lv_obj_t* sw = lv_obj_create(row);
    lv_obj_remove_flag(sw, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(sw, 96, 50);
    lv_obj_set_style_radius(sw, 25, 0);
    lv_obj_set_style_border_width(sw, 3, 0);
    lv_obj_set_style_border_color(sw, lv_color_black(), 0);
    lv_obj_set_style_bg_color(sw, on ? lv_color_black() : lv_color_white(), 0);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t* knob = lv_obj_create(sw);
    lv_obj_remove_flag(knob, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(knob, 34, 34);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(knob, on ? 0 : 3, 0);
    lv_obj_set_style_border_color(knob, lv_color_black(), 0);
    lv_obj_set_style_bg_color(knob, lv_color_white(), 0);
    lv_obj_align(knob, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -2 : 2, 0);
    lv_obj_t* st = ui::Label(row, on ? "BẬT" : "TẮT", ui::F().small);
    lv_obj_align(st, LV_ALIGN_RIGHT_MID, -104, 0);
}

// -/+ stepper with a 10-segment gauge (no slider dragging: e-ink + touch-unreliable units).
void Stepper(lv_obj_t* parent, const std::string& label, int value, int lo, int hi, int step, std::function<void(int)> set) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 150);
    lv_obj_set_style_border_width(row, 3, 0);
    lv_obj_set_style_border_color(row, lv_color_black(), 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ui::Label(row, label, ui::F().body_bold), LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(ui::Button(row, "-", [=] { set(std::max(lo, value - step)); }, 90, 70), LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(ui::Button(row, "+", [=] { set(std::min(hi, value + step)); }, 90, 70), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t* gauge = lv_obj_create(row);
    lv_obj_remove_flag(gauge, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(gauge, 390, 40);
    lv_obj_set_style_border_width(gauge, 0, 0);
    lv_obj_set_style_pad_all(gauge, 0, 0);
    lv_obj_set_style_pad_column(gauge, 6, 0);
    lv_obj_set_flex_flow(gauge, LV_FLEX_FLOW_ROW);
    lv_obj_align(gauge, LV_ALIGN_BOTTOM_MID, 0, -16);
    int lit = (value - lo) * 10 / std::max(1, hi - lo);
    for (int i = 0; i < 10; ++i) {
        lv_obj_t* seg = lv_obj_create(gauge);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(seg, 33, 40);
        lv_obj_set_style_radius(seg, 4, 0);
        lv_obj_set_style_border_width(seg, 2, 0);
        lv_obj_set_style_border_color(seg, lv_color_black(), 0);
        lv_obj_set_style_bg_color(seg, i < lit ? lv_color_black() : lv_color_white(), 0);
    }
    char v[24];
    std::snprintf(v, sizeof v, "%d%%", value);
    lv_obj_align(ui::Label(row, v, ui::F().title), LV_ALIGN_TOP_RIGHT, 0, -6);
}

void KeyValue(lv_obj_t* parent, const std::string& k, const std::string& v) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_black(), 0);
    lv_obj_set_style_pad_ver(row, 10, 0);
    lv_obj_set_style_pad_hor(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* kl = ui::Label(row, k, ui::F().small);
    lv_obj_align(kl, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t* vl = ui::Label(row, v, ui::F().body);
    lv_obj_set_width(vl, LV_PCT(100));
    lv_label_set_long_mode(vl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_top(vl, 30, 0);
}

std::string StandbyText(int s) {
    if (s == 0) return "Không";
    if (s < 60) return std::to_string(s) + " giây";
    return std::to_string(s / 60) + " phút";
}

ui::Page SectionPage(const char* title) {
    ui::Page p = ui::NewPage(title, [] { OpenSettings(); });
    lv_obj_set_style_pad_row(p.body, 14, 0);
    return p;
}

// ---------- Phím cứng ----------
// 0 Nguồn · 1 Âm lượng + · 2 Âm lượng – · 3 AI · 4 Home · 5 Trở về · 6 Trang sau
struct KeyInfo { const char* name; const char* where; const char* glyph; };
const KeyInfo kKeys[] = {
    {"Nguồn", "Cạnh máy", icon::POWER},
    {"Âm lượng +", "Cạnh máy", icon::VOLUME},
    {"Âm lượng –", "Cạnh máy", icon::VOLUME},
    {"Phím AI", "Cạnh máy (BOOT)", icon::SPARKLES},
    {"Home", "Viền dưới màn hình", icon::HOME},
    {"Trở về / trang trước", "Viền dưới màn hình", icon::CHEV_L},
    {"Trang sau", "Viền dưới màn hình", icon::CHEV_R},
};
void OpenKey(int key);

const char* VolName(settings::VolumeInReader m) {
    switch (m) {
    case settings::VolumeInReader::PageTurn: return "luôn lật trang";
    case settings::VolumeInReader::Volume: return "luôn chỉnh âm lượng";
    default: return "lật trang, chỉnh âm lượng khi đang phát nhạc";
    }
}

std::string KeySummary(int k) {
    const auto& v = settings::Get();
    auto a = [&](settings::KeySlot s) { return std::string(settings::KeyActionName(v.keys[s])); };
    switch (k) {
    case 0: return "Bấm: màn hình chờ · Giữ 3 giây: tắt máy";
    case 1: return std::string("Tăng âm lượng · Khi đọc: ") + VolName(v.volume_in_reader);
    case 2: return std::string("Giảm âm lượng · Khi đọc: ") + VolName(v.volume_in_reader);
    case 3: return "Bấm: " + a(settings::kAiClick) + " · Đúp: " + a(settings::kAiDouble) + " · Giữ: " + a(settings::kAiLong);
    case 4: return "Bấm: " + a(settings::kHomeClick) + " · Giữ: " + a(settings::kHomeLong);
    case 5: return "Bấm: trở về (khi đọc: trang trước) · Giữ: " + (v.keys[settings::kBackLong] == settings::KeyAction::None ? std::string("trở về") : a(settings::kBackLong));
    default: return "Khi đọc: trang sau · Ngoài: " + a(settings::kNextClick) + " · Giữ: " + a(settings::kNextLong);
    }
}

void KeyHeading(lv_obj_t* parent, const char* t) {
    lv_obj_t* h = ui::Label(parent, t, ui::F().small);
    lv_obj_set_style_text_letter_space(h, 3, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x5f5d58), 0);
    lv_obj_set_style_pad_top(h, 14, 0);
    lv_obj_set_style_pad_bottom(h, 4, 0);
}

void KeyRow(lv_obj_t* parent, int k) {
    lv_obj_t* b = ui::Button(parent, "", [k] { OpenKey(k); }, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_ver(b, 14, 0);
    lv_obj_set_style_min_height(b, 96, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xb9b6ae), 0);
    lv_obj_align(ui::IconLabel(b, kKeys[k].glyph), LV_ALIGN_LEFT_MID, 0, 0);
    std::string title = kKeys[k].name;
    if (k == 1) title = std::string("Âm lượng +");
    lv_obj_t* col = lv_obj_create(b);
    lv_obj_remove_flag(col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(col, 500, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 56, 0);
    ui::Label(col, title + (k == 0 ? std::string("  ") + icon::LOCK : ""), ui::F().body_bold);
    lv_obj_t* s = ui::Label(col, KeySummary(k), ui::F().small);
    lv_obj_set_style_text_color(s, lv_color_hex(0x5f5d58), 0);
    lv_obj_set_width(s, LV_PCT(100));
    lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(ui::Label(b, icon::CHEV_R, ui::F().body), LV_ALIGN_RIGHT_MID, 0, 0);
}

void Fixed(lv_obj_t* parent, const std::string& gesture, const std::string& what) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_set_size(r, LV_PCT(100), 76);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(0xb9b6ae), 0);
    lv_obj_set_style_pad_hor(r, 16, 0);
    lv_obj_remove_flag(r, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_align(ui::Label(r, gesture, ui::F().small), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* w = ui::Label(r, what, ui::F().body_bold);
    lv_obj_set_width(w, 380);
    lv_label_set_long_mode(w, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(w, LV_ALIGN_LEFT_MID, 150, 0);
    lv_obj_align(ui::Label(r, std::string(icon::LOCK), ui::F().body), LV_ALIGN_RIGHT_MID, 0, 0);
}

void OpenActionPicker(int key, settings::KeySlot slot);

void Slot(lv_obj_t* parent, int key, settings::KeySlot slot, const std::string& gesture) {
    const auto& v = settings::Get();
    lv_obj_t* r = ui::Button(parent, "", [key, slot] { OpenActionPicker(key, slot); }, LV_PCT(100), 84);
    lv_obj_set_style_pad_hor(r, 16, 0);
    lv_obj_set_style_border_width(r, 2, 0);
    lv_obj_align(ui::Label(r, gesture, ui::F().small), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* w = ui::Label(r, settings::KeyActionName(v.keys[slot]), ui::F().body_bold);
    lv_obj_align(w, LV_ALIGN_LEFT_MID, 150, 0);
    lv_obj_align(ui::Label(r, std::string("Đổi ") + icon::CHEV_R, ui::F().small), LV_ALIGN_RIGHT_MID, 0, 0);
}

void OpenKey(int k) {
    ui::Page p = ui::NewPage(kKeys[k].name, [] { OpenSection(Section::Keys); });
    lv_label_set_text(p.header_status, kKeys[k].where);
    lv_obj_set_style_pad_row(p.body, 10, 0);
    const auto& v = settings::Get();
    auto heading = [&](const char* t, const char* hint) {
        Heading(p.body, t, hint);
    };
    switch (k) {
    case 0:
        heading("Chức năng cố định", "Phím nguồn luôn giữ nguyên chức năng để máy luôn ngủ, thức và tắt được, kể cả khi màn hình hoặc cảm ứng gặp lỗi.");
        Fixed(p.body, "Bấm", "Màn hình chờ / đánh thức");
        Fixed(p.body, "Giữ 3 giây", "Tắt máy");
        break;
    case 1:
    case 2:
        heading("Chức năng cố định", "Ở mọi màn ngoài trang đọc, phím luôn chỉnh âm lượng (nhạc, ghi âm, loa).");
        Fixed(p.body, "Bấm", k == 1 ? "Tăng âm lượng 10" : "Giảm âm lượng 10");
        Fixed(p.body, "Giữ", k == 1 ? "Âm lượng tối đa" : "Tắt tiếng");
        heading("Khi đang đọc sách", "Như firmware gốc: Âm lượng + là trang trước, Âm lượng – là trang sau. Chế độ Tự động vẫn chỉnh âm lượng khi đang phát nhạc.");
        Choice(p.body, {"Tự động", "Luôn lật trang", "Luôn chỉnh âm lượng"}, (int)v.volume_in_reader, [k](int i) {
            settings::Values n = settings::Get();
            n.volume_in_reader = (settings::VolumeInReader)i;
            settings::Set(n);
            OpenKey(k);
        });
        break;
    case 3:
        heading("Chức năng", "Phím AI không có chức năng cố định: chọn tuỳ ý cho từng kiểu bấm.");
        Slot(p.body, k, settings::kAiClick, "Bấm");
        Slot(p.body, k, settings::kAiDouble, "Bấm đúp");
        Slot(p.body, k, settings::kAiLong, "Giữ");
        break;
    case 4:
        heading("Chức năng", "Mặc định: bấm về trang chủ, giữ mở kệ sách.");
        Slot(p.body, k, settings::kHomeClick, "Bấm");
        Slot(p.body, k, settings::kHomeLong, "Giữ");
        break;
    case 5:
        heading("Chức năng cố định", "Luôn có một phím để quay ra, nên phím này giữ chức năng Trở về.");
        Fixed(p.body, "Bấm", "Trở về");
        Fixed(p.body, "Khi đọc", "Trang trước (giữ: lật liên tục)");
        heading("Chức năng phụ", "Giữ phím khi không đọc sách. \"Không làm gì\" = giữ cũng là Trở về.");
        Slot(p.body, k, settings::kBackLong, "Giữ");
        break;
    default:
        heading("Chức năng cố định", "");
        Fixed(p.body, "Khi đọc", "Trang sau (giữ: lật liên tục)");
        heading("Chức năng phụ", "Ở các màn khác. Mặc định: lật trang danh sách (kệ sách, ghi chú, từ vựng…).");
        Slot(p.body, k, settings::kNextClick, "Bấm");
        Slot(p.body, k, settings::kNextLong, "Giữ");
        break;
    }
    lv_obj_t* hint = ui::Label(p.body, "Thử ngay: bấm phím trên máy, chức năng mới có hiệu lực luôn.", ui::F().small);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    ui::Show(p.screen);
}

void OpenActionPicker(int key, settings::KeySlot slot) {
    std::string title = std::string(kKeys[key].name) + " · " + settings::KeySlotName(slot);
    ui::Page p = ui::NewPage(title.c_str(), [key] { OpenKey(key); });
    lv_label_set_text(p.header_status, "Chọn chức năng");
    lv_obj_t* grid = lv_obj_create(p.body);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    const int w = (ui::kW - 2 * ui::kPad - 10) / 2;
    const auto cur = settings::Get().keys[slot];
    for (int i = 0; i < (int)settings::KeyAction::Count; ++i) {
        auto a = (settings::KeyAction)i;
        lv_obj_t* b = ui::Button(grid, settings::KeyActionName(a), [key, slot, a] {
            settings::Values n = settings::Get();
            n.keys[slot] = a;
            settings::Set(n);
            platform::Vibrate(n.haptic);
            OpenKey(key);
        }, w, 74);
        lv_obj_set_style_radius(b, 37, 0);
        ui::Invert(b, a == cur);
    }
    ui::Show(p.screen);
}

void OpenSection(Section sec) {
    using settings::Values;
    const Values& v = settings::Get();
    ui::Page p;
    switch (sec) {
    case Section::Light: {
        p = SectionPage("ĐÈN NỀN");
        lv_label_set_text(p.header_status, "Đèn nền ấm/lạnh hai màu");
        Heading(p.body, "Chế độ", "Ấm dịu mắt khi đọc buổi tối, lạnh cho ánh sáng ban ngày.");
        Choice(p.body, {"Tắt", "Ấm", "Lạnh", "Ấm + lạnh"}, (int)v.light,
               [](int i) { Update([i](Values& x) { x.light = (settings::Light)i; }, Section::Light); });
        Stepper(p.body, "Độ sáng", v.brightness, 0, 100, 5,
                [](int b) { Update([b](Values& x) { x.brightness = b; if (x.light == settings::Light::Off && b > 0) x.light = settings::Light::Warm; }, Section::Light); });
        Heading(p.body, "Phím tắt", "Bấm phím AI để bật/tắt đèn nhanh (đổi trong Phím cứng).");
        break;
    }
    case Section::Screen: {
        p = SectionPage("MÀN HÌNH");
        Heading(p.body, "Độ đậm màu đen", "Đậm hơn giúp chữ rõ trong phòng tối, nhưng tốn pin hơn khi làm mới.");
        Choice(p.body, {"Mặc định", "Vừa", "Đậm"}, v.black_depth,
               [](int i) { Update([i](Values& x) { x.black_depth = i; }, Section::Screen); });
        Heading(p.body, "Làm mới toàn màn khi đọc", "Màn e-ink để lại bóng mờ sau nhiều lần lật. Làm mới toàn màn sẽ nháy đen một lần để xoá bóng.");
        const int opts[] = {0, 5, 10, 20};
        int sel = 0;
        for (int i = 0; i < 4; ++i) if (opts[i] == v.full_refresh_pages) sel = i;
        Choice(p.body, {"Không", "Mỗi 5 trang", "Mỗi 10 trang", "Mỗi 20 trang"}, sel,
               [opts](int i) { int n = opts[i]; Update([n](Values& x) { x.full_refresh_pages = n; }, Section::Screen); });
        ui::Button(p.body, "Làm mới màn hình ngay", [] { platform::FullRefresh(); }, LV_PCT(100), 76);
        break;
    }
    case Section::Keys: {
        p = SectionPage("PHÍM CỨNG");
        lv_label_set_text(p.header_status, "7 phím: chọn chức năng phụ cho từng phím");
        lv_obj_set_style_pad_row(p.body, 0, 0);
        KeyHeading(p.body, "PHÍM CẠNH MÁY");
        for (int k : {3, 1, 2, 0}) KeyRow(p.body, k);
        KeyHeading(p.body, "PHÍM CẢM ỨNG TRÊN VIỀN");
        for (int k : {4, 5, 6}) KeyRow(p.body, k);
        lv_obj_t* gap = lv_obj_create(p.body);
        lv_obj_set_size(gap, 1, 16);
        lv_obj_set_style_border_width(gap, 0, 0);
        Toggle(p.body, "Đổi chiều phím lật trang", "Đảo trang trước / trang sau của mọi phím lật", v.reverse_page_keys,
               [] { Update([](Values& x) { x.reverse_page_keys = !x.reverse_page_keys; }, Section::Keys); });
        Toggle(p.body, "Giữ phím để lật liên tục", "Khi đọc: giữ phím lật trang để lật nhanh", v.hold_to_flip,
               [] { Update([](Values& x) { x.hold_to_flip = !x.hold_to_flip; }, Section::Keys); });
        ui::Button(p.body, "Khôi phục phím mặc định", [] {
            Update([](Values& x) {
                settings::Values d;
                for (int i = 0; i < settings::kSlotCount; ++i) x.keys[i] = d.keys[i];
                x.volume_in_reader = d.volume_in_reader;
            }, Section::Keys);
            ui::Toast("Đã khôi phục chức năng phím mặc định.");
        }, LV_PCT(100), 76);
        break;
    }
    case Section::Font: {
        p = SectionPage("PHÔNG CHỮ");
        lv_label_set_text(p.header_status, ("Đang dùng: " + ui::FontDisplayName(v.ui_font)).c_str());
        Heading(p.body, "Phông chữ hệ thống", "Dùng cho menu, danh sách, tiêu đề và màn hình chờ. Trang sách có phông riêng trong bảng Aa khi đọc.");
        std::vector<std::string> fams = {"sans", "serif"};
        for (const auto& f : platform::ListFiles(platform::FontsDir(), {".ttf", ".otf"})) fams.push_back("file:" + f);
        for (const auto& fam : fams) {
            const lv_font_t* pf = ui::F().face ? ui::F().face(fam, 32, false) : nullptr;
            const bool on = fam == v.ui_font;
            lv_obj_t* row = ui::Button(p.body, "", [fam, pf] {
                if (!pf) { ui::Toast("Không mở được phông này (cần TTF/OTF hợp lệ)."); return; }
                Update([fam](Values& x) { x.ui_font = fam; }, Section::Font);
            }, LV_PCT(100), 118);
            lv_obj_set_style_pad_hor(row, 18, 0);
            lv_obj_set_style_border_width(row, on ? 3 : 1, 0);
            lv_obj_t* n = ui::Label(row, ui::FontDisplayName(fam) + (fam.rfind("file:", 0) == 0 ? "  · thẻ nhớ" : ""), ui::F().small);
            lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 6);
            lv_obj_t* sample = ui::Label(row, pf ? "Aa  Đọc sách mỗi ngày" : "(không đọc được tệp)", pf ? pf : ui::F().body);
            lv_obj_set_width(sample, 520);
            lv_label_set_long_mode(sample, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_align(sample, LV_ALIGN_BOTTOM_LEFT, 0, -6);
            if (on) lv_obj_align(ui::IconLabel(row, icon::CHECK), LV_ALIGN_RIGHT_MID, 0, 0);
        }
        Heading(p.body, "Thêm phông", "Chép tệp .ttf / .otf vào thư mục fonts/ trên thẻ nhớ, hoặc gửi từ điện thoại qua Chuyển file (mục Phông chữ).");
        ui::Button(p.body, "Dùng phông này cho cả trang sách", [] {
            reader::Settings rs = reader::GetSettings();
            rs.font = settings::Get().ui_font;
            reader::SaveSettings(rs);
            ui::Toast("Trang sách sẽ dùng " + ui::FontDisplayName(rs.font) + ".");
        }, LV_PCT(100), 76);
        ui::Button(p.body, std::string(icon::TRANSFER) + "  Gửi phông qua Wi-Fi", [] { OpenTransfer(); }, LV_PCT(100), 76);
        break;
    }
    case Section::Goals: OpenGoals(); return;
    case Section::Assistant: {
        p = SectionPage("TRỢ LÝ AI");
        ai::Config c = ai::Load();
        lv_label_set_text(p.header_status, !v.ai_enabled ? "Đang tắt" : c.configured() ? (std::string(ai::ProviderName(c.provider)) + " · đã sẵn sàng").c_str() : "Chưa nhập khoá API");
        Toggle(p.body, "Bật trợ lý AI", v.ai_enabled ? "Hiện ở trang chủ, Ứng dụng, khi đọc sách; giữ phím AI để hỏi" : "Tắt: ẩn khỏi trang chủ và phím AI không gọi trợ lý", v.ai_enabled,
               [] { Update([](Values& x) { x.ai_enabled = !x.ai_enabled; }, Section::Assistant); });
        if (!v.ai_enabled) break;
        Heading(p.body, "Nhà cung cấp", "Dùng khoá API của chính bạn (trả phí theo lượt dùng ở trang của nhà cung cấp).");
        std::vector<std::string> names;
        for (int i = 0; i < (int)ai::Provider::Count; ++i) names.push_back(ai::ProviderName((ai::Provider)i));
        Choice(p.body, names, (int)c.provider, [](int i) {
            ai::Config x = ai::Load();
            if ((int)x.provider != i) { x.provider = (ai::Provider)i; x.model.clear(); x.transcribe_model.clear(); }
            ai::Save(x);
            OpenSection(Section::Assistant);
        });
        const auto& info = ai::Info(c.provider);
        auto row = [&](const std::string& k, const std::string& val, std::function<void()> edit) {
            lv_obj_t* r = ui::Button(p.body, "", std::move(edit), LV_PCT(100), 96);
            lv_obj_set_style_pad_hor(r, 18, 0);
            lv_obj_set_style_border_width(r, 2, 0);
            lv_obj_align(ui::Label(r, k, ui::F().small), LV_ALIGN_TOP_LEFT, 0, 8);
            lv_obj_t* v2 = ui::Label(r, val, ui::F().body_bold);
            lv_obj_set_width(v2, 480);
            lv_label_set_long_mode(v2, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_align(v2, LV_ALIGN_BOTTOM_LEFT, 0, -8);
            lv_obj_align(ui::Label(r, std::string("Sửa ") + icon::CHEV_R, ui::F().small), LV_ALIGN_RIGHT_MID, 0, 0);
        };
        auto edit = [](const char* title, const char* hint, std::string initial, std::function<void(ai::Config&, const std::string&)> set) {
            return [title, hint, initial, set] {
                ui::PromptOptions o;
                o.title = title;
                o.hint = hint;
                o.initial = initial;
                o.vietnamese = false;
                o.ok = "Lưu";
                ui::TextPrompt(o, [set](const std::string& v) {
                    ai::Config x = ai::Load();
                    set(x, v);
                    ai::Save(x);
                    OpenSection(Section::Assistant);
                });
            };
        };
        row("Khoá API", ai::MaskKey(c.api_key), edit("Khoá API", info.key_hint, "", [](ai::Config& x, const std::string& v) {
            std::string k = v;
            while (!k.empty() && (k.back() == ' ' || k.back() == '\n')) k.pop_back();
            while (!k.empty() && k.front() == ' ') k.erase(0, 1);
            x.api_key = k;
        }));
        row("Model", c.model.empty() ? ai::DefaultModel(c.provider) + "  (mặc định)" : c.model,
            edit("Model", "Để trống = model mặc định. Tên model do nhà cung cấp đặt, có thể đổi theo thời gian.", c.model,
                 [](ai::Config& x, const std::string& v) { x.model = v; }));
        if (c.provider == ai::Provider::Compatible)
            row("Địa chỉ máy chủ", c.base_url.empty() ? "chưa nhập" : c.base_url,
                edit("Địa chỉ máy chủ", "Ví dụ Ollama: http://192.168.1.10:11434/v1", c.base_url.empty() ? "http://" : c.base_url,
                     [](ai::Config& x, const std::string& v) { x.base_url = v; }));
        Toggle(p.body, "Hỏi bằng giọng nói", c.voice_supported() ? "Giữ phím AI để nói, thả ra để gửi" : "Nhà cung cấp này chưa nhận giọng nói: chỉ gõ được", c.voice && c.voice_supported(), [] {
            ai::Config x = ai::Load();
            x.voice = !x.voice;
            ai::Save(x);
            OpenSection(Section::Assistant);
        });
        ui::Button(p.body, std::string(icon::SPARKLES) + "  Thử kết nối", [] {
            ai::Config x = ai::Load();
            if (!x.configured()) { ui::Toast("Nhập khoá API trước đã."); return; }
            ui::Toast("Đang thử…", 8000);
            platform::Http(ai::ChatRequest(x, {{"user", "Trả lời đúng một câu: xin chào."}}, {}), [x](const platform::HttpResponse& r) {
                std::string t, e;
                if (ai::ParseChat(x.provider, r.status, r.body, t, e)) ui::Toast("Kết nối được: " + t.substr(0, 120), 3000);
                else ui::Toast("Lỗi: " + (r.status == 0 && !r.error.empty() ? r.error : e), 4000);
            });
        }, LV_PCT(100), 76);
        lv_obj_t* wifi = ui::Button(p.body, std::string(icon::WIFI) + "  Cài đặt từ máy tính / điện thoại", [] { OpenTransferFor("ai"); }, LV_PCT(100), 76);
        ui::Invert(wifi, true);
        Heading(p.body, "Quyền riêng tư", "Khoá API lưu trên thẻ nhớ (.metalio/ai.json), không nằm trong firmware. Khi hỏi, câu hỏi, tên sách, trang đang mở và đoạn bạn chọn được gửi tới nhà cung cấp đã chọn.");
        break;
    }
    case Section::Battery: {
        p = SectionPage("PIN");
        const auto b = platform::Battery();
        char st[96];
        std::snprintf(st, sizeof st, "%d%% · %s · %.2f V", b.percent, b.charging ? "đang sạc" : "đang dùng pin", b.voltage_mv / 1000.0);
        lv_label_set_text(p.header_status, st);
        // Now
        lv_obj_t* now = lv_obj_create(p.body);
        lv_obj_set_size(now, LV_PCT(100), 110);
        lv_obj_set_style_border_width(now, 2, 0);
        lv_obj_set_style_radius(now, 12, 0);
        lv_obj_set_style_pad_hor(now, 20, 0);
        lv_obj_remove_flag(now, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(ui::BatteryIcon(now, 56, false, -1, 1), LV_ALIGN_LEFT_MID, 0, 0);
        char d[128];
        std::snprintf(d, sizeof d, "%s\n%.2f V · %+d mA", b.charging ? "Đang sạc" : "Đang dùng pin", b.voltage_mv / 1000.0, b.current_ma);
        lv_obj_t* dl = ui::Label(now, d, ui::F().body);
        lv_obj_align(dl, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_RIGHT, 0);

        Heading(p.body, "Dòng sạc", "Như firmware gốc. 0,5 A sạc chậm, mát và giữ pin bền nhất; 1,5 A sạc nhanh, cần củ sạc từ 2 A.");
        const int mas[] = {500, 1000, 1500};
        int cur = 1;
        for (int i = 0; i < 3; ++i) if (platform::ChargeCurrentMa() == mas[i]) cur = i;
        Choice(p.body, {"0,5 A · bền pin", "1 A · mặc định", "1,5 A · sạc nhanh"}, cur, [mas](int i) {
            if (!platform::SetChargeCurrentMa(mas[i])) ui::Toast("Chip sạc không nhận mức này, giữ nguyên mức cũ.");
            OpenSection(Section::Battery);
        });

        Heading(p.body, "Biểu tượng pin", "Hiện ở thanh trạng thái trang chủ và khi đọc sách.");
        lv_obj_t* styles = lv_obj_create(p.body);
        lv_obj_set_size(styles, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(styles, 0, 0);
        lv_obj_set_style_pad_all(styles, 0, 0);
        lv_obj_set_style_pad_gap(styles, 10, 0);
        lv_obj_set_flex_flow(styles, LV_FLEX_FLOW_ROW);
        lv_obj_remove_flag(styles, LV_OBJ_FLAG_SCROLLABLE);
        const char* names[] = {"Vòng tròn", "Thanh pin", "Chỉ số %"};
        const int bw = (ui::kW - 2 * ui::kPad - 20) / 3;
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* b2 = ui::Button(styles, "", [i] { Update([i](Values& x) { x.battery_style = i; }, Section::Battery); }, bw, 130);
            lv_obj_set_style_border_width(b2, v.battery_style == i ? 4 : 1, 0);
            lv_obj_align(ui::BatteryIcon(b2, 40, false, i, 1), LV_ALIGN_TOP_MID, 0, 12);
            lv_obj_t* n = ui::Label(b2, names[i], v.battery_style == i ? ui::F().body_bold : ui::F().small);
            lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -10);
        }
        Toggle(p.body, "Hiện % pin", "Số phần trăm trên thanh trạng thái", v.battery_percent,
               [] { Update([](Values& x) { x.battery_percent = !x.battery_percent; }, Section::Battery); });
        Toggle(p.body, "Hiện % khi đọc sách", "Tắt: khi đọc chỉ hiện biểu tượng pin", v.battery_percent_reader,
               [] { Update([](Values& x) { x.battery_percent_reader = !x.battery_percent_reader; }, Section::Battery); });
        break;
    }
    case Section::Haptic: {
        p = SectionPage("RUNG PHẢN HỒI");
        Heading(p.body, "Cường độ rung", "Rung nhẹ khi chạm nút và bấm phím.");
        Choice(p.body, {"Tắt", "Nhẹ", "Vừa", "Mạnh"}, v.haptic,
               [](int i) { Update([i](Values& x) { x.haptic = i; }, Section::Haptic); });
        break;
    }
    case Section::Power: {
        p = SectionPage("NGUỒN & MÀN CHỜ");
        Heading(p.body, "Vào màn hình chờ sau", "Không tự ngủ khi đang nghe nhạc, ghi âm hoặc chuyển file.");
        const int opts[] = {30, 60, 120, 300, 0};
        int sel = 2;
        for (int i = 0; i < 5; ++i) if (opts[i] == v.standby_seconds) sel = i;
        Choice(p.body, {"30 giây", "1 phút", "2 phút", "5 phút", "Không"}, sel,
               [opts](int i) { int n = opts[i]; Update([n](Values& x) { x.standby_seconds = n; }, Section::Power); });
        Heading(p.body, "Tắt máy khi chờ quá");
        const int off[] = {60, 1440, 0};
        int osel = 2;
        for (int i = 0; i < 3; ++i) if (off[i] == v.poweroff_minutes) osel = i;
        Choice(p.body, {"1 giờ", "1 ngày", "Không bao giờ"}, osel,
               [off](int i) { int n = off[i]; Update([n](Values& x) { x.poweroff_minutes = n; }, Section::Power); });
        Heading(p.body, "Màn hình chờ hiển thị", "Thông tin chủ máy luôn hiện ở chân màn hình chờ.");
        Choice(p.body, {"Bìa sách đang đọc", "Thông tin chủ máy", "Đồng hồ"}, (int)v.standby_screen,
               [](int i) { Update([i](Values& x) { x.standby_screen = (settings::Standby)i; }, Section::Power); });
        ui::Button(p.body, "Xem thử màn hình chờ", [] { ShowStandby(); }, LV_PCT(100), 76);
        break;
    }
    case Section::Wifi: {
        p = SectionPage("WI-FI");
        Toggle(p.body, "Wi-Fi", "Cần cho Khám phá, Nhà thông minh, Chuyển file", v.wifi,
               [] { Update([](Values& x) { x.wifi = !x.wifi; }, Section::Wifi); });
        if (v.wifi) {
            Heading(p.body, "Mạng");
            auto nets = platform::WifiScan();
            if (nets.empty()) ui::Label(p.body, "Không thấy mạng nào.", ui::F().body);
            for (const auto& n : nets) {
                std::string s = n.ssid + (n.connected ? "  ·  đã kết nối" : "") + (n.secured ? "" : "  ·  mở");
                lv_obj_t* b = ui::Button(p.body, s, [] { ui::Toast("Nhập mật khẩu Wi-Fi bằng bàn phím ảo của firmware trên máy thật."); }, LV_PCT(100), 76);
                ui::Invert(b, n.connected);
            }
            ui::Button(p.body, "Quét lại", [] { OpenSection(Section::Wifi); }, LV_PCT(100), 76);
        }
        break;
    }
    case Section::Bluetooth: {
        p = SectionPage("MÁY LẬT TRANG");
        lv_label_set_text(p.header_status, "Máy lật trang / remote Bluetooth (BLE)");
        Toggle(p.body, "Dùng máy lật trang Bluetooth", "", v.bt_page_turner,
               [] { Update([](Values& x) { x.bt_page_turner = !x.bt_page_turner; }, Section::Bluetooth); });
        if (v.bt_page_turner) {
            Toggle(p.body, "Tự kết nối khi mở máy", "", v.bt_auto_connect,
                   [] { Update([](Values& x) { x.bt_auto_connect = !x.bt_auto_connect; }, Section::Bluetooth); });
            Heading(p.body, "Thiết bị", "Bật máy lật trang ở chế độ ghép đôi rồi bấm Tìm.");
            lv_obj_t* list = lv_obj_create(p.body);
            lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_border_width(list, 0, 0);
            lv_obj_set_style_pad_all(list, 0, 0);
            lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(list, 10, 0);
            ui::Button(p.body, "Tìm máy lật trang", [list] {
                lv_obj_clean(list);
                auto found = platform::BluetoothScan();
                if (found.empty()) ui::Label(list, "Không tìm thấy thiết bị nào.", ui::F().body);
                for (const auto& n : found) ui::Button(list, n, [] {}, LV_PCT(100), 76);
            }, LV_PCT(100), 76);
        }
        break;
    }
    case Section::Storage: {
        p = SectionPage("BỘ NHỚ & USB");
        auto st = platform::Storage();
        Heading(p.body, "Thẻ nhớ");
        if (!st.present) {
            ui::Label(p.body, "Không có thẻ nhớ.", ui::F().body);
        } else {
            uint64_t used = st.total - st.free;
            int pct = st.total ? (int)(used * 100 / st.total) : 0;
            ui::Progress(p.body, pct, ui::kW - 2 * ui::kPad, 16);
            ui::Label(p.body, "Đã dùng " + Size(used) + " / " + Size(st.total) + " · còn trống " + Size(st.free), ui::F().body);
        }
        auto count = [](const std::string& dir, std::vector<std::string> ext) {
            return std::to_string(platform::ListFiles(dir, ext).size());
        };
        library::Invalidate();
        KeyValue(p.body, "Sách (mọi thư mục trên thẻ)", std::to_string(library::Books().size()) + " cuốn");
        KeyValue(p.body, "Phông chữ (fonts/) · Từ điển (dict/)", count(platform::FontsDir(), {".ttf", ".otf"}) + " phông · " +
                 count(platform::DictDir(), {".ifo", ".tsv"}) + " từ điển");
        KeyValue(p.body, "Nhạc (music/)", count(platform::MusicDir(), {".mp3", ".wav"}) + " bài");
        KeyValue(p.body, "Ghi âm (recordings/)", count(platform::RecordingsDir(), {".wav"}) + " bản ghi");
        Toggle(p.body, "Chế độ ổ USB", "Cắm cáp vào máy tính để chép sách như ổ đĩa", v.usb_disk, [] {
            if (!platform::UsbDiskSupported()) { ui::Toast("Chế độ ổ USB chỉ có trên máy thật (usb_virtual_disk)."); return; }
            Update([](Values& x) { x.usb_disk = !x.usb_disk; }, Section::Storage);
        });
        ui::Button(p.body, "Chuyển file qua Wi-Fi  ›", [] { OpenTransfer(); }, LV_PCT(100), 76);
        if (st.present) ui::Button(p.body, "Định dạng thẻ nhớ…", [] { OpenFormat(); }, LV_PCT(100), 76);
        break;
    }
    case Section::Language: {
        p = SectionPage("NGÔN NGỮ");
        Choice(p.body, {"Tiếng Việt", "English"}, v.language == "en" ? 1 : 0, [](int i) {
            if (i == 1) { ui::Toast("Bản tiếng Anh chưa có, giao diện giữ tiếng Việt."); return; }
            Update([](Values& x) { x.language = "vi"; }, Section::Language);
        });
        break;
    }
    case Section::Owner:
        OpenOwner();
        return;
    case Section::About: {
        p = SectionPage("THÔNG TIN MÁY");
        auto d = platform::GetDeviceInfo();
        auto st = platform::Storage();
        owner::Info o = owner::Get();
        KeyValue(p.body, "Máy", d.model);
        KeyValue(p.body, "Chủ máy", o.empty() ? "Chưa đặt" : o.name + (o.contact.empty() ? "" : " · " + o.contact));
        KeyValue(p.body, "Phần mềm", d.firmware);
        KeyValue(p.body, "Chip", d.chip);
        KeyValue(p.body, "Màn hình", d.panel);
        KeyValue(p.body, "Wi-Fi", platform::WifiName().empty() ? "Chưa kết nối" : platform::WifiName());
        KeyValue(p.body, "Pin", std::to_string(platform::BatteryPercent()) + "%" + (platform::Charging() ? " · đang sạc" : ""));
        KeyValue(p.body, "Thẻ nhớ", st.present ? Size(st.free) + " trống / " + Size(st.total) : "Không có");
        KeyValue(p.body, "Địa chỉ MAC", d.mac);
        break;
    }
    }
    ui::Show(p.screen);
}

struct Row { Section sec; const char* icon; const char* title; std::function<std::string()> summary; };

// ---- Định dạng thẻ nhớ ----

struct FormatState {
    bool keep = true;
    bool armed = false;            // first press done, waiting for the confirming press
    lv_timer_t* disarm = nullptr;
    lv_obj_t* go = nullptr;
    lv_obj_t* go_label = nullptr;
    std::shared_ptr<bool> alive;
};

void FormatProgressPage(bool keep) {
    ui::Page p = ui::NewPage("ĐỊNH DẠNG THẺ NHỚ", nullptr);
    ui::SetBackHandler([] {});          // no way out while the card is being rewritten
    auto alive = ui::Alive(p.screen);
    lv_label_set_text(p.header_status, "Không tắt máy và không rút thẻ nhớ");
    lv_obj_t* stage = ui::Label(p.body, "Chuẩn bị…", ui::F().title);
    lv_obj_t* bar = ui::Progress(p.body, 0, ui::kW - 2 * ui::kPad, 24);
    lv_obj_t* pct = ui::Label(p.body, "0%", ui::F().body);
    ui::Show(p.screen);
    platform::AudioStop();
    if (platform::RecGetStatus().state == platform::RecState::Recording) platform::RecStop();
    platform::TransferStop();
    platform::StorageFormat(keep,
        [alive, stage, bar, pct](int percent, const std::string& text) {
            if (!*alive) return;
            ui::NoteActivity();
            lv_label_set_text(stage, text.c_str());
            lv_label_set_text_fmt(pct, "%d%%", percent);
            lv_obj_t* fill = lv_obj_get_child(bar, 0);
            lv_obj_set_width(fill, LV_PCT(std::max(1, percent)));
            lv_obj_remove_flag(fill, LV_OBJ_FLAG_HIDDEN);
        },
        [alive, keep](bool ok, const std::string& err) {
            if (!*alive) return;
            ui::Page d = ui::NewPage("ĐỊNH DẠNG THẺ NHỚ", [] { OpenLauncher(); });
            if (ok) {
                lv_label_set_text(d.header_status, "Hoàn tất");
                ui::Label(d.body, "Đã định dạng thẻ nhớ.", ui::F().title);
                lv_obj_t* l = ui::Label(d.body, std::string("Thẻ nhớ đã trống, các thư mục books/, music/, recordings/ được tạo lại.") +
                                                    (keep ? " Thông tin chủ máy và cài đặt được giữ nguyên." : " Thông tin chủ máy và cài đặt đã về mặc định."),
                                        ui::F().body);
                lv_obj_set_width(l, LV_PCT(100));
                lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
                ui::Button(d.body, "Chép sách qua Wi-Fi", [] { OpenTransfer(); }, LV_PCT(100));
                ui::Button(d.body, "Về kệ sách", [] { OpenLauncher(); }, LV_PCT(100));
            } else {
                lv_label_set_text(d.header_status, "Không thành công");
                ui::Label(d.body, "Định dạng thất bại.", ui::F().title);
                lv_obj_t* l = ui::Label(d.body, err + "\nThử lại, hoặc định dạng thẻ trên máy tính (FAT32).", ui::F().body);
                lv_obj_set_width(l, LV_PCT(100));
                lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
                ui::Button(d.body, "Về Cài đặt", [] { OpenSettings(); }, LV_PCT(100));
            }
            ui::Show(d.screen);
        });
}

void RenderFormatButton(FormatState* s) {
    lv_label_set_text(s->go_label, s->armed ? "Bấm lần nữa để XOÁ VĨNH VIỄN" : "Định dạng thẻ nhớ");
    ui::Invert(s->go, s->armed);
}

void OpenFormat(bool keep) {
    auto* s = new FormatState();
    s->keep = keep;
    ui::Page p = ui::NewPage("ĐỊNH DẠNG THẺ NHỚ", [] { OpenSection(Section::Storage); });
    s->alive = ui::Alive(p.screen);
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) {
        auto* st = static_cast<FormatState*>(lv_event_get_user_data(e));
        if (st->disarm) lv_timer_delete(st->disarm);
        delete st;
    }, LV_EVENT_DELETE, s);
    lv_obj_set_style_pad_row(p.body, 14, 0);
    lv_label_set_text(p.header_status, "FAT32 · không thể hoàn tác");

    auto st = platform::Storage();
    auto n = [](const std::string& dir, std::vector<std::string> ext) { return platform::ListFiles(dir, ext).size(); };
    char what[256];
    std::snprintf(what, sizeof what, "%zu cuốn sách, %zu bài nhạc, %zu bản ghi âm, tiến độ đọc và thống kê đọc sách.",
                  n(platform::GetConfig().books_dir, {".epub", ".txt"}), n(platform::MusicDir(), {".mp3", ".wav"}),
                  n(platform::RecordingsDir(), {".wav"}));
    lv_obj_t* warn = lv_obj_create(p.body);
    lv_obj_set_size(warn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(warn, lv_color_black(), 0);
    lv_obj_set_style_radius(warn, 10, 0);
    lv_obj_set_style_border_width(warn, 0, 0);
    lv_obj_set_style_pad_all(warn, 20, 0);
    lv_obj_set_flex_flow(warn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(warn, 8, 0);
    lv_obj_remove_flag(warn, LV_OBJ_FLAG_SCROLLABLE);
    auto white = [](lv_obj_t* l) { lv_obj_set_style_text_color(l, lv_color_white(), 0); return l; };
    white(ui::Label(warn, "Toàn bộ thẻ nhớ sẽ bị xoá", ui::F().title));
    lv_obj_t* w = white(ui::Label(warn, std::string("Sẽ mất: ") + what + " Đang dùng " + Size(st.total - st.free) + ".", ui::F().body));
    lv_obj_set_width(w, LV_PCT(100));
    lv_label_set_long_mode(w, LV_LABEL_LONG_MODE_WRAP);

    Toggle(p.body, "Giữ chủ máy & cài đặt", "Tên, liên hệ, đèn nền, phím… được giữ lại", s->keep,
           [s] { OpenFormat(!s->keep); });
    lv_obj_t* note = ui::Label(p.body, "Nên chép sách ra máy tính trước (Chuyển file › Tải). Định dạng kiểu FAT32, dùng được cho chế độ ổ USB.", ui::F().small);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);

    s->go = ui::Button(p.body, "", [s] {
        if (!s->armed) {   // first press only arms; it falls back after 5 s
            s->armed = true;
            RenderFormatButton(s);
            if (s->disarm) lv_timer_delete(s->disarm);
            s->disarm = lv_timer_create([](lv_timer_t* t) {
                auto* st = static_cast<FormatState*>(lv_timer_get_user_data(t));
                st->armed = false;
                st->disarm = nullptr;
                lv_timer_delete(t);
                RenderFormatButton(st);
            }, 5000, s);
            return;
        }
        bool keep = s->keep;
        FormatProgressPage(keep);
    }, LV_PCT(100), 96);
    s->go_label = ui::Label(s->go, "", ui::F().body_bold);
    lv_obj_center(s->go_label);
    RenderFormatButton(s);
    ui::Button(p.body, "Huỷ", [] { OpenSection(Section::Storage); }, LV_PCT(100), 76);
    ui::Show(p.screen);
}

}  // namespace

void OpenFontSettings() { OpenSection(Section::Font); }
void OpenSettingsSection(int index) { OpenSection((Section)index); }   // simulator script "open settings:N"
void OpenKeySettings(int key) { OpenKey(key); }
void OpenAssistantSettings() { OpenSection(Section::Assistant); }
void OpenStorageSettings() { OpenSection(Section::Storage); }

void OpenSettings() {
    ui::Page p = ui::NewPage("CÀI ĐẶT", [] { OpenApps(); });
    lv_obj_set_style_pad_row(p.body, 8, 0);
    lv_label_set_text(p.header_status, "Đèn nền, phím cứng, màn hình, kết nối");
    const auto& v = settings::Get();
    const Row rows[] = {
        {Section::Light, icon::SUN, "Đèn nền", [&] { return v.light == settings::Light::Off ? std::string("Tắt") : std::string(settings::LightName(v.light)) + " · " + std::to_string(v.brightness) + "%"; }},
        {Section::Screen, icon::DEVICE, "Màn hình", [&] { return std::string(v.black_depth == 0 ? "Đen mặc định" : v.black_depth == 1 ? "Đen vừa" : "Đen đậm") + " · " + (v.full_refresh_pages ? "làm mới mỗi " + std::to_string(v.full_refresh_pages) + " trang" : std::string("không làm mới")); }},
        {Section::Font, icon::FONT, "Phông chữ", [&] { return ui::FontDisplayName(v.ui_font); }},
        {Section::Assistant, icon::SPARKLES, "Trợ lý AI", [&] { ai::Config c = ai::Load(); return !v.ai_enabled ? std::string("Đang tắt") : c.configured() ? std::string(ai::ProviderName(c.provider)) + " · khoá " + ai::MaskKey(c.api_key) : std::string("Chưa cài: ChatGPT, Gemini, Claude…"); }},
        {Section::Battery, icon::BAT_FULL, "Pin", [&] {
            static const char* st[] = {"vòng tròn", "thanh pin", "chỉ số %"};
            return std::to_string(platform::BatteryPercent()) + "% · sạc " + (platform::ChargeCurrentMa() == 500 ? "0,5 A" : platform::ChargeCurrentMa() == 1500 ? "1,5 A" : "1 A") + " · biểu tượng " + st[v.battery_style]; }},
        {Section::Goals, icon::TARGET, "Mục tiêu đọc", [&] { return (v.goal_minutes ? std::to_string(v.goal_minutes) + " phút mỗi ngày" : std::string("Không đặt mục tiêu ngày")) + " · " + std::to_string(v.goal_books) + " cuốn/năm"; }},
        {Section::Keys, icon::KEYBOARD, "Phím cứng", [&] { return std::string("AI: ") + settings::KeyActionName(v.keys[settings::kAiClick]) + " · Home: " + settings::KeyActionName(v.keys[settings::kHomeClick]) + (v.reverse_page_keys ? " · đảo chiều lật" : ""); }},
        {Section::Haptic, icon::VIBRATE, "Rung phản hồi", [&] { const char* n[] = {"Tắt", "Nhẹ", "Vừa", "Mạnh"}; return std::string(n[v.haptic]); }},
        {Section::Power, icon::POWER, "Nguồn & màn chờ", [&] { return "Chờ sau " + StandbyText(v.standby_seconds) + " · " + settings::StandbyName(v.standby_screen); }},
        {Section::Wifi, icon::WIFI, "Wi-Fi", [&] { return v.wifi ? (platform::WifiName().empty() ? std::string("Bật · chưa kết nối") : "Bật · " + platform::WifiName()) : std::string("Tắt"); }},
        {Section::Bluetooth, icon::BT, "Máy lật trang Bluetooth", [&] { return std::string(v.bt_page_turner ? "Bật" : "Tắt"); }},
        {Section::Storage, icon::SD, "Bộ nhớ & USB", [] { auto s = platform::Storage(); return s.present ? "Còn trống " + Size(s.free) : std::string("Không có thẻ nhớ"); }},
        {Section::Language, icon::LANG, "Ngôn ngữ", [&] { return std::string(v.language == "en" ? "English" : "Tiếng Việt"); }},
        {Section::Owner, icon::USER, "Chủ máy", [] { auto o = owner::Get(); return o.empty() ? std::string("Chưa đặt") : o.name; }},
        {Section::About, icon::INFO, "Thông tin máy", [] { return platform::GetDeviceInfo().firmware; }},
    };
    for (const auto& r : rows) {
        Section sec = r.sec;
        lv_obj_t* b = ui::Button(p.body, "", [sec] { OpenSection(sec); }, LV_PCT(100), 88);
        lv_obj_set_style_pad_hor(b, 16, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_align(ui::IconLabel(b, r.icon), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(ui::Label(b, r.title, ui::F().body_bold), LV_ALIGN_TOP_LEFT, 56, 2);
        lv_obj_t* s = ui::Label(b, r.summary(), ui::F().small);
        lv_obj_set_size(s, 520, lv_font_get_line_height(ui::F().small));
        lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 56, -2);
        lv_obj_align(ui::Label(b, "›", ui::F().title), LV_ALIGN_RIGHT_MID, 0, 0);
    }
    ui::Show(p.screen);
}

}  // namespace screens
