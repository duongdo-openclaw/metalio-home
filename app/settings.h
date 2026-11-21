// Device settings, modelled on the stock Metalio settings tabs (frontlight, screen, keys, haptic,
// power, Bluetooth page turner, Wi-Fi, storage/USB, language). Stored on the SD card as JSON.
#pragma once
#include <string>

namespace settings {

enum class Light { Off, Warm, Cool, Both };                 // stock: SETTINGS_FRONTLIGHT_*
// What a configurable key does (Cài đặt › Phím cứng). Power and volume keep their own functions:
// see KeySlot for what can be changed. Values are stored by number: append only.
enum class KeyAction {
    None, Home, Shelf, Back, PrevPage, NextPage, QuickSettings, Frontlight, LightUp, LightDown,
    ReaderMenu, Bookmark, Dictionary, Notes, Vocab, Record, Music, Refresh, Standby, Rotate,
    Transfer, ContinueReading, Assistant, Count
};
// The configurable gestures of the Metalio keys (firmware: BOOT GPIO61, cover keys vk_home /
// vk_prev / vk_next at touch Y=1600, TCA P10/P11 volume, P17 power).
enum KeySlot {
    kAiClick, kAiDouble, kAiLong,     // phím AI (BOOT): bấm, bấm đúp, giữ
    kHomeClick, kHomeLong,            // phím Home trên viền
    kBackLong,                        // phím Trở về: giữ (ngoài trang đọc)
    kNextClick, kNextLong,            // phím Trang sau: ngoài trang đọc
    kSlotCount
};
enum class VolumeInReader { Auto, PageTurn, Volume };   // Auto: page turn, volume while music plays
enum class Standby { Cover, Owner, Clock };

struct Values {
    // Đèn nền
    Light light = Light::Off;
    int brightness = 40;          // 0..100
    // Màn hình
    int black_depth = 0;          // 0 default, 1 medium, 2 strong (stock: SCREEN_BLACK_DEPTH_*)
    int full_refresh_pages = 10;  // full (flashing) refresh every N page turns; 0 = never
    // Phím cứng
    bool reverse_page_keys = false;
    bool hold_to_flip = true;     // holding a page key keeps turning (vk_page_repeat)
    // Like the stock firmware, holding the AI key talks to the assistant.
    KeyAction keys[kSlotCount] = {KeyAction::Frontlight, KeyAction::QuickSettings, KeyAction::Assistant,
                                  KeyAction::Home, KeyAction::Shelf, KeyAction::None,
                                  KeyAction::NextPage, KeyAction::None};
    VolumeInReader volume_in_reader = VolumeInReader::Auto;
    // Rung
    int haptic = 2;               // 0 off, 1 weak, 2 medium, 3 strong
    // Nguồn
    int standby_seconds = 120;    // stock offers 30/60/120 s; 0 = never
    int poweroff_minutes = 0;     // 0 = never
    Standby standby_screen = Standby::Cover;
    // Kết nối
    bool wifi = true;
    bool bt_page_turner = false;
    bool bt_auto_connect = true;
    // Bộ nhớ
    bool usb_disk = false;        // expose the SD card as a USB drive
    // Hướng màn hình (firmware display_orient: 0 dọc, 1 ngang trái, 2 ngang phải, 3 dọc ngược).
    // Landscape applies while reading; other screens stay portrait (Kindle/Kobo behaviour).
    int orient = 0;
    // Ngôn ngữ
    std::string language = "vi";
    // Phông chữ hệ thống (menu, danh sách, tiêu đề): "sans" (không chân), "serif" (có chân)
    // or "file:<name>" from <sd>/fonts. The reader keeps its own font (Aa sheet).
    std::string ui_font = "sans";
    // Trợ lý AI: off hides it everywhere (home tab, apps, reader) and the AI key no longer calls it
    bool ai_enabled = true;
    // Pin
    int battery_style = 0;        // 0 vòng tròn, 1 thanh pin, 2 chỉ số %
    bool battery_percent = true;  // số % trên thanh trạng thái
    bool battery_percent_reader = false;   // số % khi đọc sách (mặc định chỉ biểu tượng)
    // Mục tiêu đọc
    int goal_minutes = 20;        // per day, 0 = no daily goal
    int goal_books = 12;          // books finished per year, 0 = none
};

const Values& Get();
void Set(const Values& v);          // saves and notifies the platform (light, black depth…)
const char* LightName(Light l);
const char* KeyActionName(KeyAction a);
const char* KeySlotName(KeySlot s);
const char* StandbyName(Standby s);

}  // namespace settings
