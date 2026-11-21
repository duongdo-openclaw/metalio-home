// Quick settings: pulled down from the top edge on any screen (or AI key / home status line).
// Frontlight, radios, sleep, refresh, screen orientation, and font size while reading.
#include <cstdio>
#include <ctime>

#include "platform.h"
#include "reading_state.h"
#include "screens.h"
#include "settings.h"
#include "icons.h"
#include "ui_common.h"

namespace screens {
namespace {

lv_obj_t* g_qs = nullptr;
ui::Action g_prev_back;
lv_obj_t* g_prev_screen = nullptr;   // the screen whose back handler we borrowed
const lv_color_t kInk = lv_color_hex(0x111110);
const lv_color_t kMuted = lv_color_hex(0x5f5d58);

void Close() {
    if (!g_qs) return;
    lv_obj_delete_async(g_qs);
    g_qs = nullptr;
    // Give the back key back to the screen underneath, but only if it is still the same screen:
    // a tile may have replaced it (rotation rebuilds the reader), and its handler would dangle.
    if (lv_screen_active() == g_prev_screen) ui::SetBackHandler(g_prev_back);
    g_prev_back = nullptr;
    g_prev_screen = nullptr;
}

void Reopen() {   // re-render after a change (cheap: a dozen objects); keeps the borrowed handler
    if (!g_qs) return;
    ui::Action back = g_prev_back;
    lv_obj_t* screen = g_prev_screen;
    lv_obj_delete_async(g_qs);
    g_qs = nullptr;
    OpenQuickSettings();
    g_prev_back = back;
    g_prev_screen = screen;
}

void Change(const std::function<void(settings::Values&)>& f) {
    settings::Values v = settings::Get();
    f(v);
    settings::Set(v);
    platform::Vibrate(v.haptic);
    Reopen();
}

lv_obj_t* Plain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

lv_obj_t* Row(lv_obj_t* parent) {
    lv_obj_t* r = Plain(parent);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(r, 10, 0);
    return r;
}

lv_obj_t* Caption(lv_obj_t* parent, const std::string& t) {
    lv_obj_t* l = ui::Label(parent, t, ui::F().small);
    lv_obj_set_style_text_color(l, kMuted, 0);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    return l;
}

// Square tile: icon + label; black when the feature is on.
void Tile(lv_obj_t* parent, const char* icon, const std::string& text, bool on, ui::Action a, int w) {
    lv_obj_t* b = ui::Button(parent, "", std::move(a), w, 112);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_pad_all(b, 6, 0);
    lv_obj_align(ui::IconLabel(b, icon), LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t* l = ui::Label(b, text, ui::F().small);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -6);
    ui::Invert(b, on);
}

lv_obj_t* Chip(lv_obj_t* parent, const std::string& text, bool on, ui::Action a, int w) {
    lv_obj_t* b = ui::Button(parent, text, std::move(a), w, 60);
    lv_obj_set_style_radius(b, 30, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_pad_hor(b, w == LV_SIZE_CONTENT ? 13 : 8, 0);   // four orientation chips fit one row
    ui::Invert(b, on);
    return b;
}

}  // namespace

bool QuickSettingsOpen() { return g_qs != nullptr; }

void OpenQuickSettings() {
    if (g_qs) { Close(); return; }
    if (IsStandby()) return;
    const auto& v = settings::Get();
    const int W = ui::ScreenW(), H = ui::ScreenH();
    const bool reading = !ui::ReaderPath().empty();
    const bool land = W > H;

    // Full-screen catcher: tapping the dimmed-free area below the sheet closes it.
    g_qs = Plain(lv_layer_top());
    lv_obj_set_size(g_qs, W, H);
    lv_obj_add_flag(g_qs, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_qs, [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) Close();
    }, LV_EVENT_CLICKED, nullptr);
    g_prev_back = ui::GetBackHandler();
    g_prev_screen = lv_screen_active();
    ui::SetBackHandler([] { Close(); });

    lv_obj_t* sheet = Plain(g_qs);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sheet, W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_white(), 0);
    lv_obj_set_style_border_side(sheet, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(sheet, 4, 0);
    lv_obj_set_style_border_color(sheet, kInk, 0);
    lv_obj_set_style_radius(sheet, 0, 0);
    lv_obj_set_style_pad_hor(sheet, 26, 0);
    lv_obj_set_style_pad_top(sheet, 18, 0);
    lv_obj_set_style_pad_bottom(sheet, 14, 0);
    lv_obj_set_style_pad_row(sheet, 14, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    const int inner = W - 52;

    // Header: clock, date, battery
    lv_obj_t* head = Plain(sheet);
    lv_obj_set_size(head, LV_PCT(100), 64);
    std::time_t t = std::time(nullptr);
    char clock[16], date[48];
    std::strftime(clock, sizeof clock, "%H:%M", std::localtime(&t));
    std::strftime(date, sizeof date, "%d/%m/%Y", std::localtime(&t));
    lv_obj_align(ui::Label(head, clock, ui::F().reader(46, false)), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* d = ui::Label(head, std::string(date) + "  ·  pin " + std::to_string(platform::BatteryPercent()) + "%" +
                                      (platform::Charging() ? " · đang sạc" : ""), ui::F().small);
    lv_obj_set_style_text_color(d, kMuted, 0);
    lv_obj_align(d, LV_ALIGN_LEFT_MID, 150, 4);
    lv_obj_t* x = ui::Button(head, "Đóng", [] { Close(); }, 110, 56);
    lv_obj_set_style_radius(x, 28, 0);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, 0, 0);

    // Tiles
    const int cols = land ? 8 : 4;
    const int tw = (inner - (cols - 1) * 10) / cols;
    lv_obj_t* tiles = Row(sheet);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Tile(tiles, icon::HOME, "Trang chủ", false, [] { Close(); OpenLauncher(); }, tw);
    Tile(tiles, icon::WIFI, "Wi-Fi", v.wifi, [] { Change([](settings::Values& x) { x.wifi = !x.wifi; }); }, tw);
    Tile(tiles, icon::BT, "Bluetooth", v.bt_page_turner, [] { Change([](settings::Values& x) { x.bt_page_turner = !x.bt_page_turner; }); }, tw);
    Tile(tiles, icon::SUN, "Đèn nền", v.light != settings::Light::Off, [] {
        Change([](settings::Values& x) { x.light = x.light == settings::Light::Off ? settings::Light::Warm : settings::Light::Off; });
    }, tw);
    Tile(tiles, icon::REFRESH, "Làm mới", false, [] { Close(); platform::FullRefresh(); }, tw);
    Tile(tiles, icon::MOON, "Ngủ", false, [] { Close(); ShowStandby(); }, tw);
    Tile(tiles, icon::TRANSFER, "Chuyển file", false, [] { Close(); OpenTransfer(); }, tw);
    Tile(tiles, icon::SETTINGS, "Cài đặt", false, [] { Close(); OpenSettings(); }, tw);

    // Frontlight
    Caption(sheet, "ĐÈN NỀN");
    lv_obj_t* lrow = Row(sheet);
    lv_obj_set_flex_align(lrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char* modes[] = {"Tắt", "Ấm", "Lạnh", "Ấm + lạnh"};
    for (int i = 0; i < 4; ++i)
        Chip(lrow, modes[i], (int)v.light == i, [i] { Change([i](settings::Values& x) { x.light = (settings::Light)i; }); }, LV_SIZE_CONTENT);
    lv_obj_t* brow = Row(sheet);
    Chip(brow, "-", false, [] { Change([](settings::Values& x) { x.brightness = std::max(0, x.brightness - 10); if (x.light == settings::Light::Off) x.light = settings::Light::Warm; }); }, 80);
    lv_obj_t* gauge = Plain(brow);
    lv_obj_set_size(gauge, inner - 2 * 80 - 30, 40);
    lv_obj_set_flex_flow(gauge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gauge, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const int lit = v.light == settings::Light::Off ? 0 : (v.brightness + 5) / 10;
    const int segw = (inner - 2 * 80 - 30 - 9 * 6) / 10;
    for (int i = 0; i < 10; ++i) {
        lv_obj_t* seg = Plain(gauge);
        lv_obj_set_size(seg, segw, 16 + i * 2);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_set_style_border_width(seg, 2, 0);
        lv_obj_set_style_border_color(seg, kInk, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, i < lit ? kInk : lv_color_white(), 0);
    }
    Chip(brow, "+", false, [] { Change([](settings::Values& x) { x.brightness = std::min(100, x.brightness + 10); if (x.light == settings::Light::Off) x.light = settings::Light::Warm; }); }, 80);

    // Orientation
    Caption(sheet, reading ? "XOAY MÀN HÌNH" : "XOAY MÀN HÌNH · NGANG KHI ĐỌC SÁCH");
    lv_obj_t* orow = Row(sheet);
    lv_obj_set_flex_align(orow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char* orients[] = {"Dọc", "Ngang trái", "Ngang phải", "Ngược"};
    for (int i = 0; i < 4; ++i) {
        Chip(orow, orients[i], v.orient == i, [i] {
            settings::Values n = settings::Get();
            n.orient = i;
            settings::Set(n);
            Close();
            const std::string book = ui::ReaderPath();
            // The open screen was laid out for the old size: rebuild it in the new orientation.
            if (!book.empty()) OpenReader(book);
            else {
                if (i == 1 || i == 2) ui::Toast("Màn ngang dùng khi đọc sách. Mở một cuốn sách để xem.", 2200);
                OpenLauncher();
            }
        }, LV_SIZE_CONTENT);
    }

    // Reading: font size right here
    if (reading) {
        Caption(sheet, "CỠ CHỮ");
        lv_obj_t* frow = Row(sheet);
        reader::Settings rs = reader::GetSettings();
        auto font = [](int delta) {
            reader::Settings st = reader::GetSettings();
            st.font_px = std::clamp(st.font_px + delta, 22, 64);
            reader::SaveSettings(st);
            Close();
            OpenReader(ui::ReaderPath());   // re-paginates and keeps the reading position
        };
        Chip(frow, "A-", false, [font] { font(-2); }, 120);
        lv_obj_t* val = ui::Label(frow, std::to_string(rs.font_px) + " px", ui::F().title);
        lv_obj_set_width(val, inner - 2 * 120 - 30);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
        Chip(frow, "A+", false, [font] { font(+2); }, 120);
    }

    lv_obj_t* grip = Plain(sheet);   // visual handle
    lv_obj_set_size(grip, 80, 6);
    lv_obj_set_style_radius(grip, 3, 0);
    lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(grip, kInk, 0);
    lv_obj_set_style_align(grip, LV_ALIGN_TOP_MID, 0);
    if (land) return;   // landscape is only 680 px tall: no room for the hint line
    lv_obj_t* hint = Caption(sheet, "Chạm ra ngoài hoặc bấm Quay lại để đóng");
    lv_obj_set_style_text_letter_space(hint, 0, 0);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

}  // namespace screens
