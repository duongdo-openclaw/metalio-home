// "Ứng dụng": everything that is not reading — owner card on top, app tiles below.
#include "owner.h"
#include "platform.h"
#include "icons.h"
#include "screens.h"
#include "settings.h"
#include "ui_common.h"

namespace screens {
namespace {

const lv_color_t kHair = lv_color_hex(0xb9b6ae);

// Launcher-style tile: line icon in a thin ring, name underneath (Kobo "More" / Kindle apps).
void Tile(lv_obj_t* parent, const char* glyph, const char* title, void (*open)(), int w) {
    lv_obj_t* b = ui::Button(parent, "", open, w, 176);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t* ring = lv_obj_create(b);
    lv_obj_remove_flag(ring, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(ring, 88, 88);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t* ic = ui::IconLabel(ring, glyph, ui::IconFont(44));
    lv_obj_center(ic);
    lv_obj_t* t = ui::Label(b, title, ui::F().small);
    lv_obj_set_width(t, w - 4);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(t, -2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 100);
}

lv_obj_t* Group(lv_obj_t* parent, const char* name) {
    lv_obj_t* h = ui::Label(parent, name, ui::F().small);
    lv_obj_set_style_text_letter_space(h, 3, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x5f5d58), 0);
    lv_obj_set_style_pad_top(h, 6, 0);
    lv_obj_t* grid = lv_obj_create(parent);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(grid, 1, 0);
    lv_obj_set_style_border_side(grid, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(grid, kHair, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_top(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    return grid;
}

}  // namespace

void OpenApps() {
    ui::Page p = ui::NewPage("ỨNG DỤNG", nullptr);
    ui::ReserveBottomNav(p);
    ui::SetBackHandler([] { OpenLauncher(); });
    lv_label_set_text(p.header_status, "Từ điển, từ vựng, ghi chú, nghe nhạc và cài đặt máy");

    // Owner card (like Kindle's "Device info / If found")
    owner::Info o = owner::Get();
    lv_obj_t* card = ui::Button(p.body, "", [] { OpenOwner(); }, LV_PCT(100), 166);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_t* av = lv_obj_create(card);  // monogram avatar
    lv_obj_remove_flag(av, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(av, 100, 100);
    lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(av, lv_color_black(), 0);
    lv_obj_set_style_border_width(av, 0, 0);
    lv_obj_align(av, LV_ALIGN_LEFT_MID, 0, 0);
    std::string first = o.empty() ? "?" : owner::FirstName(o);
    size_t len = first.empty() ? 0 : ((unsigned char)first[0] < 0x80 ? 1 : (unsigned char)first[0] < 0xE0 ? 2 : 3);
    lv_obj_t* ini = ui::Label(av, first.substr(0, len), ui::F().serif_title);
    lv_obj_set_style_text_color(ini, lv_color_white(), 0);
    lv_obj_center(ini);
    lv_obj_t* small = ui::Label(card, "CHỦ MÁY", ui::F().small);
    lv_obj_set_style_text_letter_space(small, 2, 0);
    lv_obj_align(small, LV_ALIGN_TOP_LEFT, 130, 0);
    lv_obj_t* name = ui::Label(card, o.empty() ? "Chưa đặt tên chủ máy" : o.name, ui::F().serif_book);
    lv_obj_set_size(name, ui::kW - 2 * ui::kPad - 36 - 130 - 50, lv_font_get_line_height(ui::F().serif_book));
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 130, 30);
    lv_obj_t* contact = ui::Label(card, o.contact.empty() ? "Chạm để thêm số điện thoại / email liên hệ" : o.contact, ui::F().body);
    lv_obj_set_size(contact, ui::kW - 2 * ui::kPad - 36 - 130, lv_font_get_line_height(ui::F().body));
    lv_label_set_long_mode(contact, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(contact, LV_ALIGN_TOP_LEFT, 130, 80);
    lv_obj_align(ui::IconLabel(card, icon::CHEV_R), LV_ALIGN_RIGHT_MID, 0, 0);

    const int w = (ui::kW - 2 * ui::kPad) / 4;
    lv_obj_t* g = Group(p.body, "ĐỌC & HỌC");
    const bool ai = settings::Get().ai_enabled;
    if (ai) Tile(g, icon::SPARKLES, "Trợ lý AI", [] { OpenAssistant(); }, w);
    Tile(g, icon::DICT, "Từ điển", [] { OpenDictionary(); }, w);
    Tile(g, icon::VOCAB, "Học từ vựng", OpenVocab, w);
    Tile(g, icon::NOTES, "Ghi chú", OpenNotes, w);
    if (!ai) Tile(g, icon::TARGET, "Mục tiêu đọc", OpenGoals, w);
    g = Group(p.body, "NGHE & NHÀ");
    Tile(g, icon::MUSIC, "Nghe nhạc", OpenMusic, w);
    Tile(g, icon::MIC, "Ghi âm", OpenRecorder, w);
    Tile(g, icon::SMARTHOME, "Nhà thông minh", OpenSmartHome, w);
    Tile(g, icon::DOWNLOAD, "Thư viện online", OpenOnlineLibraries, w);
    g = Group(p.body, "MÁY");
    Tile(g, icon::TRANSFER, "Chuyển file", OpenTransfer, w);
    if (ai) Tile(g, icon::TARGET, "Mục tiêu đọc", OpenGoals, w);
    else Tile(g, icon::FONT, "Phông chữ", OpenFontSettings, w);
    Tile(g, icon::SETTINGS, "Cài đặt", OpenSettings, w);
    Tile(g, icon::TOUCH, "Kiểm tra cảm ứng", OpenTouchTest, w);

    ui::BottomNav(p.screen, ui::Tab::Apps);
    ui::Show(p.screen);
}

}  // namespace screens
