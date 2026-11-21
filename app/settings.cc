#include "settings.h"

#include <cJSON.h>

#include <algorithm>

#include "platform.h"

namespace settings {
namespace {

Values g_values;
bool g_loaded = false;

std::string File() { return platform::StateDir() + "/settings.json"; }

int Int(const cJSON* o, const char* k, int def) {
    const cJSON* v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valueint : def;
}
bool Bool(const cJSON* o, const char* k, bool def) {
    const cJSON* v = cJSON_GetObjectItem(o, k);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

void Load() {
    g_loaded = true;
    std::string s;
    if (!platform::ReadText(File(), s)) return;
    cJSON* o = cJSON_Parse(s.c_str());
    if (cJSON_IsObject(o)) {
        Values d;
        Values& v = g_values;
        v.light = (Light)std::clamp(Int(o, "light", (int)d.light), 0, 3);
        v.brightness = std::clamp(Int(o, "brightness", d.brightness), 0, 100);
        v.black_depth = std::clamp(Int(o, "black_depth", d.black_depth), 0, 2);
        v.full_refresh_pages = std::clamp(Int(o, "full_refresh_pages", d.full_refresh_pages), 0, 100);
        v.reverse_page_keys = Bool(o, "reverse_page_keys", d.reverse_page_keys);
        v.hold_to_flip = Bool(o, "hold_to_flip", d.hold_to_flip);
        const cJSON* keys = cJSON_GetObjectItem(o, "keys");
        if (cJSON_IsArray(keys)) {
            for (int i = 0; i < kSlotCount && i < cJSON_GetArraySize(keys); ++i) {
                const cJSON* k = cJSON_GetArrayItem(keys, i);
                if (cJSON_IsNumber(k)) v.keys[i] = (KeyAction)std::clamp(k->valueint, 0, (int)KeyAction::Count - 1);
            }
        } else if (cJSON_GetObjectItem(o, "ai_press") || cJSON_GetObjectItem(o, "ai_hold")) {   // settings from before per-key mapping (old AiKey enum)
            const KeyAction old[] = {KeyAction::Frontlight, KeyAction::Home, KeyAction::ReaderMenu,
                                     KeyAction::Record, KeyAction::None, KeyAction::QuickSettings};
            v.keys[kAiClick] = old[std::clamp(Int(o, "ai_press", 0), 0, 5)];
            v.keys[kAiLong] = old[std::clamp(Int(o, "ai_hold", 3), 0, 5)];
        }
        v.volume_in_reader = (VolumeInReader)std::clamp(Int(o, "volume_in_reader", 0), 0, 2);
        v.orient = std::clamp(Int(o, "orient", d.orient), 0, 3);
        v.haptic = std::clamp(Int(o, "haptic", d.haptic), 0, 3);
        v.standby_seconds = std::clamp(Int(o, "standby_seconds", d.standby_seconds), 0, 3600);
        v.poweroff_minutes = std::clamp(Int(o, "poweroff_minutes", d.poweroff_minutes), 0, 10080);
        v.standby_screen = (Standby)std::clamp(Int(o, "standby_screen", (int)d.standby_screen), 0, 2);
        v.wifi = Bool(o, "wifi", d.wifi);
        v.bt_page_turner = Bool(o, "bt_page_turner", d.bt_page_turner);
        v.bt_auto_connect = Bool(o, "bt_auto_connect", d.bt_auto_connect);
        v.usb_disk = Bool(o, "usb_disk", d.usb_disk);
        const cJSON* l = cJSON_GetObjectItem(o, "language");
        if (cJSON_IsString(l)) v.language = l->valuestring;
        const cJSON* uf = cJSON_GetObjectItem(o, "ui_font");
        if (cJSON_IsString(uf) && *uf->valuestring) v.ui_font = uf->valuestring;
        v.ai_enabled = Bool(o, "ai_enabled", d.ai_enabled);
        v.battery_style = std::clamp(Int(o, "battery_style", d.battery_style), 0, 2);
        v.battery_percent = Bool(o, "battery_percent", d.battery_percent);
        v.battery_percent_reader = Bool(o, "battery_percent_reader", d.battery_percent_reader);
        v.goal_minutes = std::clamp(Int(o, "goal_minutes", d.goal_minutes), 0, 600);
        v.goal_books = std::clamp(Int(o, "goal_books", d.goal_books), 0, 365);
    }
    cJSON_Delete(o);
}

}  // namespace

const Values& Get() {
    if (!g_loaded) {
        Load();
        platform::ApplySettings(g_values);
    }
    return g_values;
}

void Set(const Values& v) {
    g_values = v;
    g_loaded = true;
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "light", (int)v.light);
    cJSON_AddNumberToObject(o, "brightness", v.brightness);
    cJSON_AddNumberToObject(o, "black_depth", v.black_depth);
    cJSON_AddNumberToObject(o, "full_refresh_pages", v.full_refresh_pages);
    cJSON_AddBoolToObject(o, "reverse_page_keys", v.reverse_page_keys);
    cJSON_AddBoolToObject(o, "hold_to_flip", v.hold_to_flip);
    int ks[kSlotCount];
    for (int i = 0; i < kSlotCount; ++i) ks[i] = (int)v.keys[i];
    cJSON_AddItemToObject(o, "keys", cJSON_CreateIntArray(ks, kSlotCount));
    cJSON_AddNumberToObject(o, "volume_in_reader", (int)v.volume_in_reader);
    cJSON_AddNumberToObject(o, "haptic", v.haptic);
    cJSON_AddNumberToObject(o, "standby_seconds", v.standby_seconds);
    cJSON_AddNumberToObject(o, "poweroff_minutes", v.poweroff_minutes);
    cJSON_AddNumberToObject(o, "standby_screen", (int)v.standby_screen);
    cJSON_AddBoolToObject(o, "wifi", v.wifi);
    cJSON_AddBoolToObject(o, "bt_page_turner", v.bt_page_turner);
    cJSON_AddBoolToObject(o, "bt_auto_connect", v.bt_auto_connect);
    cJSON_AddBoolToObject(o, "usb_disk", v.usb_disk);
    cJSON_AddStringToObject(o, "language", v.language.c_str());
    cJSON_AddNumberToObject(o, "orient", v.orient);
    cJSON_AddStringToObject(o, "ui_font", v.ui_font.c_str());
    cJSON_AddNumberToObject(o, "goal_minutes", v.goal_minutes);
    cJSON_AddNumberToObject(o, "battery_style", v.battery_style);
    cJSON_AddBoolToObject(o, "ai_enabled", v.ai_enabled);
    cJSON_AddBoolToObject(o, "battery_percent", v.battery_percent);
    cJSON_AddBoolToObject(o, "battery_percent_reader", v.battery_percent_reader);
    cJSON_AddNumberToObject(o, "goal_books", v.goal_books);
    char* s = cJSON_Print(o);
    if (s) platform::WriteText(File(), s);
    cJSON_free(s);
    cJSON_Delete(o);
    platform::ApplySettings(g_values);
}

const char* LightName(Light l) {
    switch (l) {
    case Light::Warm: return "Ấm";
    case Light::Cool: return "Lạnh";
    case Light::Both: return "Ấm + lạnh";
    default: return "Tắt";
    }
}

const char* KeyActionName(KeyAction a) {
    switch (a) {
    case KeyAction::None: return "Không làm gì";
    case KeyAction::Home: return "Về trang chủ";
    case KeyAction::Shelf: return "Mở kệ sách";
    case KeyAction::Back: return "Trở về";
    case KeyAction::PrevPage: return "Trang trước";
    case KeyAction::NextPage: return "Trang sau";
    case KeyAction::QuickSettings: return "Cài đặt nhanh";
    case KeyAction::Frontlight: return "Bật/tắt đèn nền";
    case KeyAction::LightUp: return "Tăng độ sáng";
    case KeyAction::LightDown: return "Giảm độ sáng";
    case KeyAction::ReaderMenu: return "Menu khi đọc";
    case KeyAction::Bookmark: return "Đánh dấu trang";
    case KeyAction::Dictionary: return "Từ điển";
    case KeyAction::Notes: return "Ghi chú";
    case KeyAction::Vocab: return "Học từ vựng";
    case KeyAction::Record: return "Ghi âm nhanh";
    case KeyAction::Music: return "Phát / dừng nhạc";
    case KeyAction::Refresh: return "Làm mới màn hình";
    case KeyAction::Standby: return "Màn hình chờ";
    case KeyAction::Rotate: return "Xoay màn hình";
    case KeyAction::Transfer: return "Chuyển file";
    case KeyAction::ContinueReading: return "Đọc tiếp sách dở";
    case KeyAction::Assistant: return "Trợ lý AI";
    default: return "?";
    }
}

const char* KeySlotName(KeySlot s) {
    switch (s) {
    case kAiClick: return "Bấm";
    case kAiDouble: return "Bấm đúp";
    case kAiLong: return "Giữ";
    case kHomeClick: return "Bấm";
    case kHomeLong: return "Giữ";
    case kBackLong: return "Giữ (ngoài trang đọc)";
    case kNextClick: return "Bấm (ngoài trang đọc)";
    case kNextLong: return "Giữ (ngoài trang đọc)";
    default: return "?";
    }
}

const char* StandbyName(Standby s) {
    switch (s) {
    case Standby::Owner: return "Thông tin chủ máy";
    case Standby::Clock: return "Đồng hồ";
    default: return "Bìa sách đang đọc";
    }
}

}  // namespace settings
