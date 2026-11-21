// E-ink friendly LVGL helpers shared by the screens.
// Rules for e-ink: pure black/white, thick borders, no animation, big targets (>= 88 px),
// every action reachable with the hardware keys too (touch can be broken on some units).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"

namespace ui {

// Firmware (boards/metalio-e-ink4-plus/config.h): panel ED047TC2_1216 is 1216x684 landscape,
// used as 684x1216 portrait; ordinary UI is inset 2 px per side -> LVGL resolution 680x1212.
constexpr int kPanelW = 684, kPanelH = 1216, kInset = 2;
constexpr int kW = kPanelW - 2 * kInset;   // 680 = DISPLAY_CONTENT_W
constexpr int kH = kPanelH - 2 * kInset;   // 1212 = DISPLAY_CONTENT_H
constexpr double kPpi = 297.0;            // 4.7" 16:9 at 1216x684

// Current LVGL resolution: 680x1212 in portrait, 1212x680 when the reader runs in landscape.
int ScreenW();
int ScreenH();
constexpr int kPad = 20;
constexpr int kTouch = 88; // minimum target height

enum class FontSize { Small, Body, Title, Big };
struct Fonts {
    const lv_font_t* small = nullptr;
    const lv_font_t* body = nullptr;
    const lv_font_t* body_bold = nullptr;
    const lv_font_t* title = nullptr;
    const lv_font_t* big = nullptr;
    const lv_font_t* icon = nullptr;       // Lucide line icons (icons.h), 36 px
    const lv_font_t* icon_big = nullptr;   // 56 px, player transport buttons, app tiles
    std::function<const lv_font_t*(int px)> icon_px;   // icons at any size
    // Every text font falls back to Lucide, so labels can mix text and icon:: glyphs.
    std::function<const lv_font_t*(int px, bool bold)> reader;  // book text at any size (serif)
    // Any face: "serif", "sans", or "file:<name>" from <sd>/fonts (TTF/OTF). nullptr if unavailable.
    std::function<const lv_font_t*(const std::string& family, int px, bool bold)> face;
    const lv_font_t* serif_title = nullptr;   // greeting, ~40 px bold
    const lv_font_t* serif_book = nullptr;    // book titles, ~30 px bold
    const lv_font_t* serif_italic = nullptr;  // quotes, ~22 px italic
};
void SetFonts(const Fonts& f);
// System font (settings::ui_font): rebuilds small/body/title/big (+ serif accents for non-default
// faces) from F().face and re-themes the display. Screens built afterwards use the new face.
void ApplyUiFont(const std::string& family);
std::string FontDisplayName(const std::string& family);   // "Không chân (Noto Sans)", file name…
const Fonts& F();

using Action = std::function<void()>;

// Screen with a header (title + back button). Returns the content container.
struct Page {
    lv_obj_t* screen = nullptr;
    lv_obj_t* header_status = nullptr;
    lv_obj_t* body = nullptr;
};
Page NewPage(const char* title, Action on_back);
void Show(lv_obj_t* screen);       // load without animation, delete the previous screen

lv_obj_t* Label(lv_obj_t* parent, const std::string& text, const lv_font_t* font);
lv_obj_t* Button(lv_obj_t* parent, const std::string& text, Action on_click, int w = LV_SIZE_CONTENT,
                 int h = kTouch);
void Invert(lv_obj_t* obj, bool black);      // black background + white text when true
// The firmware renders ordinary UI in 1-bit (LV_COLOR_FORMAT_I1) and switches the whole frame to
// 8-bit grey only while an object tagged as grey content is visible (EpdSetContentRole).
enum class GrayRole { Cover, ReaderText, Illustration, Shelf };   // firmware: EpdContentRole::Shelf for the bookshelf
void MarkGray(lv_obj_t* obj, GrayRole role);
bool GrayContentVisible();

// Flag that turns false when `screen` is deleted; async callbacks must check it.
std::shared_ptr<bool> Alive(lv_obj_t* screen);
void Toast(const std::string& text, uint32_t ms = 2500);

// Global back handler (ESC key / BACK cover key).
void SetBackHandler(Action a);
void InvokeBack();

// Hardware page-turn keys (Metalio cover keys). Returns false when the screen has no handler,
// so the host can fall back to focus navigation.
void SetPageKeyHandler(std::function<void(int dir)> fn);   // dir: +1 next, -1 previous
bool InvokePageKey(int dir);           // honours "đổi chiều phím lật trang"

// The reader announces the book it is about to show; every other screen clears it on Show().
// Quick settings (font size) and screen rotation (landscape is reader-only) use this.
void SetReaderOpen(const std::string& path);
const std::string& ReaderPath();

// Applies settings::orient for the screen about to be built: landscape only when `reading`.
// Call before creating a screen's objects (NewPage and the reader do this).
void PrepareOrientation(bool reading);

// Quick settings (swipe down from the top edge). The touch driver feeds every pointer sample;
// returns true while the gesture owns the finger (the host then reports the press as lost).
void SetQuickSettingsHandler(Action open);
bool FeedPointer(bool pressed, int x, int y);
Action GetBackHandler();

// AI key "open reader menu" hook, set by the reader.
void SetAiMenuHandler(Action a);
bool InvokeAiMenu();
// What the reader shows (book, chapter, page text): context for the AI assistant.
struct ReadingContext { std::string book_path, title, chapter, page_text; };
void SetReadingContextProvider(std::function<ReadingContext()> fn);
bool CurrentReadingContext(ReadingContext& out);   // false when not reading
// "Đánh dấu trang" key action, set by the reader (cleared when another screen is shown).
void SetBookmarkHandler(Action a);
bool InvokeBookmark();

// Activity tracking for standby (every touch/key calls NoteActivity).
void NoteActivity();
uint32_t IdleMs();

// Book cover: real image when decodable (JPEG/PNG), otherwise a typographic cover.
lv_obj_t* Cover(lv_obj_t* parent, const std::vector<uint8_t>& image, const std::string& title,
                const std::string& author, int w, int h);

// Horizontal progress bar 0..100, flat e-ink style.
lv_obj_t* Progress(lv_obj_t* parent, int percent, int w, int h);
// Kobo-style bottom navigation: Kệ sách (home) · Khám phá (Drive) · Trợ lý AI · Ứng dụng.
constexpr int kNavH = 104;
enum class Tab { Home, Discover, Assistant, Apps };
void BottomNav(lv_obj_t* screen, Tab active);
void ReserveBottomNav(Page& p);
void SetNavHandler(std::function<void(Tab)> fn);   // set once by the app (screens live outside ui)   // shrink the page body so the nav does not cover it
lv_obj_t* IconLabel(lv_obj_t* parent, const char* symbol, const lv_font_t* font = nullptr);
const lv_font_t* IconFont(int px);
// Battery indicator in the style chosen in Cài đặt › Pin (ring that shrinks as the charge goes
// down, classic battery bar, or the number only), `h` px tall, vertically centred. reader = true
// uses the "show % while reading" option. SetBattery refreshes it (and re-reads the settings).
// style / show_percent = -1: from the settings (pass values for the settings previews).
lv_obj_t* BatteryIcon(lv_obj_t* parent, int h, bool reader, int style = -1, int show_percent = -1);
void SetBattery(lv_obj_t* icon, int percent, bool charging);   // Lucide at px (falls back to F().icon)
std::string FormatTime(uint64_t ms);

// Full-screen text entry with the on-screen keyboard (layer_top, above any screen). `vietnamese`
// starts in Telex mode (VN/EN toggle on the bar). on_done gets the text; Back/Huỷ cancels.
struct PromptOptions {
    std::string title, hint, initial, placeholder;
    bool multiline = false;
    bool vietnamese = true;
    std::string ok = "Xong";
};
void TextPrompt(const PromptOptions& o, std::function<void(const std::string&)> on_done);
bool TextPromptOpen();
lv_obj_t* StyledKeyboard(lv_obj_t* parent, lv_obj_t* textarea, int h);   // e-ink keyboard look   // m:ss or h:mm:ss

}  // namespace ui
