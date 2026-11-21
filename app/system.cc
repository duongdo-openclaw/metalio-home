// System behaviour shared by all screens: standby screen (with the owner's "if found" details),
// automatic standby after inactivity, and the AI / power hardware keys.
#include <algorithm>
#include <cstdio>
#include <ctime>

#include "book.h"
#include "owner.h"
#include "platform.h"
#include "reading_state.h"
#include "screens.h"
#include "settings.h"
#include "ui_common.h"

namespace screens {
namespace {

lv_obj_t* g_standby = nullptr;
settings::Light g_last_light = settings::Light::Warm;  // restored by the AI key

void CloseStandby() {
    if (!g_standby) return;
    lv_obj_delete(g_standby);
    g_standby = nullptr;
    ui::NoteActivity();
    platform::FullRefresh();  // waking clears the standby image completely
    // The standby screen is portrait; a book read in landscape is rebuilt in its orientation.
    if (!ui::ReaderPath().empty() && platform::DisplayOrientation() != settings::Get().orient) OpenReader(ui::ReaderPath());
}

void OwnerStrip(lv_obj_t* parent) {
    owner::Info o = owner::Get();
    lv_obj_t* strip = lv_obj_create(parent);
    lv_obj_remove_flag(strip, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(strip, ui::kW, 220);
    lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_bg_color(strip, lv_color_black(), 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, 24, 0);
    auto white = [](lv_obj_t* l) { lv_obj_set_style_text_color(l, lv_color_white(), 0); return l; };
    if (o.empty()) {
        lv_obj_t* l = white(ui::Label(strip, "Máy đọc sách Metalio\nĐặt thông tin chủ máy trong Cài đặt › Chủ máy.", ui::F().body));
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        return;
    }
    lv_obj_t* who = white(ui::Label(strip, "Máy đọc sách của " + o.name, ui::F().serif_book));
    lv_obj_align(who, LV_ALIGN_TOP_LEFT, 0, 0);
    if (!o.contact.empty()) lv_obj_align(white(ui::Label(strip, "Liên hệ: " + o.contact, ui::F().body_bold)), LV_ALIGN_TOP_LEFT, 0, 50);
    if (!o.message.empty()) {
        lv_obj_t* m = white(ui::Label(strip, o.message, ui::F().small));
        lv_obj_set_width(m, ui::kW - 48);
        lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_align(m, LV_ALIGN_TOP_LEFT, 0, o.contact.empty() ? 50 : 94);
    }
}

}  // namespace

bool IsStandby() { return g_standby != nullptr; }

void ShowStandby() {
    if (g_standby) return;
    if (QuickSettingsOpen()) OpenQuickSettings();   // toggles it closed
    const int o = platform::DisplayOrientation();
    if (o == 1 || o == 2) platform::SetDisplayOrientation(0);   // standby art is laid out in portrait
    g_standby = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_standby, ui::kW, ui::kH);
    lv_obj_set_style_radius(g_standby, 0, 0);
    lv_obj_set_style_border_width(g_standby, 0, 0);
    lv_obj_set_style_pad_all(g_standby, 0, 0);
    lv_obj_set_style_bg_color(g_standby, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g_standby, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_standby, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_standby, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_standby, [](lv_event_t*) { CloseStandby(); }, LV_EVENT_CLICKED, nullptr);

    const auto& v = settings::Get();
    std::time_t t = std::time(nullptr);
    std::string book_path = reader::LastBook();
    if (v.standby_screen == settings::Standby::Cover && !book_path.empty()) {
        book::Book b;
        std::string err, mime;
        std::vector<uint8_t> cover;
        if (b.Open(book_path, err)) b.Cover(cover, mime);
        lv_obj_t* c = ui::Cover(g_standby, cover, b.Title(), b.Author(), 520, 780);
        lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 60);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* cap = ui::Label(g_standby, "Đang đọc: " + b.Title(), ui::F().serif_italic);
        lv_obj_set_width(cap, 520);
        lv_label_set_long_mode(cap, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 860);
    } else if (v.standby_screen == settings::Standby::Owner && !owner::Get().empty()) {
        owner::Info o = owner::Get();
        lv_obj_t* a = ui::Label(g_standby, "Máy đọc sách này thuộc về", ui::F().serif_italic);
        lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 300);
        lv_obj_t* n = ui::Label(g_standby, o.name, ui::F().reader(56, true));
        lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 360);
        lv_obj_t* c = ui::Label(g_standby, o.contact, ui::F().title);
        lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 460);
    } else {
        char clock[16], date[64];
        std::strftime(clock, sizeof clock, "%H:%M", std::localtime(&t));
        static const char* wd[] = {"Chủ nhật", "Thứ hai", "Thứ ba", "Thứ tư", "Thứ năm", "Thứ sáu", "Thứ bảy"};
        std::tm* tm = std::localtime(&t);
        std::snprintf(date, sizeof date, "%s, %02d/%02d/%04d", wd[tm->tm_wday], tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
        lv_obj_align(ui::Label(g_standby, clock, ui::F().reader(150, true)), LV_ALIGN_TOP_MID, 0, 280);
        lv_obj_align(ui::Label(g_standby, date, ui::F().serif_book), LV_ALIGN_TOP_MID, 0, 480);
    }
    lv_obj_t* hint = ui::Label(g_standby, "Đang ở màn hình chờ · chạm hoặc bấm phím để mở", ui::F().small);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -234);
    OwnerStrip(g_standby);
    platform::FullRefresh();
}

bool WakeIfStandby() {
    if (!g_standby) return false;
    CloseStandby();
    return true;
}

void InstallSystem() {
    ui::SetQuickSettingsHandler([] { OpenQuickSettings(); });
    lv_timer_create([](lv_timer_t*) {
        const auto& v = settings::Get();
        if (g_standby || v.standby_seconds == 0) return;
        if (ui::IdleMs() < (uint32_t)v.standby_seconds * 1000) return;
        // Never sleep in the middle of audio, recording or a transfer.
        auto play = platform::AudioStatus().state;
        auto rec = platform::RecGetStatus().state;
        if (play == platform::PlayState::Playing || rec == platform::RecState::Recording ||
            rec == platform::RecState::Paused || platform::TransferRunning() || platform::StorageFormatting()) {
            ui::NoteActivity();
            return;
        }
        ShowStandby();
    }, 1000, nullptr);
    ui::NoteActivity();
}

namespace {

bool Reading() { return !ui::ReaderPath().empty(); }

void ChangeLight(int delta) {
    settings::Values n = settings::Get();
    if (n.light == settings::Light::Off) n.light = g_last_light;
    n.brightness = std::max(0, std::min(100, n.brightness + delta));
    settings::Set(n);
    ui::Toast("Độ sáng " + std::to_string(n.brightness) + "%", 900);
}

void Volume(int delta, int absolute = -1) {
    int v = absolute >= 0 ? absolute : platform::AudioStatus().volume + delta;
    v = std::max(0, std::min(100, v));
    platform::AudioSetVolume(v);
    ui::Toast(v == 0 ? "Đã tắt tiếng" : v == 100 ? "Âm lượng tối đa" : "Âm lượng " + std::to_string(v), 900);
}

// Volume keys inside the reader: turn pages (stock firmware), except when the setting says volume,
// or in Auto while music is playing, so volume always stays reachable.
bool VolumeTurnsPages() {
    if (!Reading()) return false;
    switch (settings::Get().volume_in_reader) {
    case settings::VolumeInReader::PageTurn: return true;
    case settings::VolumeInReader::Volume: return false;
    default: return platform::AudioStatus().state != platform::PlayState::Playing;
    }
}

}  // namespace

void RunKeyAction(settings::KeyAction a) {
    using settings::KeyAction;
    const auto& v = settings::Get();
    switch (a) {
    case KeyAction::None: break;
    case KeyAction::Home: OpenLauncher(); break;
    case KeyAction::Shelf: OpenLibrary(); break;
    case KeyAction::Back: ui::InvokeBack(); break;
    case KeyAction::PrevPage: ui::InvokePageKey(-1); break;
    case KeyAction::NextPage: ui::InvokePageKey(+1); break;
    case KeyAction::QuickSettings: OpenQuickSettings(); break;
    case KeyAction::Frontlight: {
        settings::Values n = v;
        if (n.light == settings::Light::Off) {
            n.light = g_last_light;
            if (n.brightness == 0) n.brightness = 40;
        } else {
            g_last_light = n.light;
            n.light = settings::Light::Off;
        }
        settings::Set(n);
        ui::Toast(n.light == settings::Light::Off ? "Đã tắt đèn nền" : std::string("Đèn nền: ") + settings::LightName(n.light) + " " + std::to_string(n.brightness) + "%", 1200);
        break;
    }
    case KeyAction::LightUp: ChangeLight(+10); break;
    case KeyAction::LightDown: ChangeLight(-10); break;
    case KeyAction::ReaderMenu:
        if (!ui::InvokeAiMenu()) ui::Toast("Menu này chỉ có khi đang đọc sách.", 1500);
        break;
    case KeyAction::Bookmark:
        if (!ui::InvokeBookmark()) ui::Toast("Đánh dấu trang chỉ dùng khi đang đọc sách.", 1500);
        break;
    case KeyAction::Dictionary: OpenDictionary(); break;
    case KeyAction::Notes: OpenNotes(); break;
    case KeyAction::Vocab: OpenVocab(); break;
    case KeyAction::Record: OpenRecorder(); break;
    case KeyAction::Music: {
        auto st = platform::AudioStatus();
        if (st.state == platform::PlayState::Playing) { platform::AudioPause(true); ui::Toast("Đã dừng nhạc", 900); }
        else if (st.state == platform::PlayState::Paused) { platform::AudioPause(false); ui::Toast("Tiếp tục phát nhạc", 900); }
        else OpenMusic();
        break;
    }
    case KeyAction::Refresh: platform::FullRefresh(); break;
    case KeyAction::Standby: ShowStandby(); break;
    case KeyAction::Rotate: {
        settings::Values n = v;
        n.orient = (n.orient + 1) % 4;
        settings::Set(n);
        static const char* names[] = {"Dọc", "Ngang trái", "Ngang phải", "Dọc ngược"};
        if (Reading()) OpenReader(ui::ReaderPath());
        else OpenLauncher();
        ui::Toast(std::string("Hướng màn hình: ") + names[n.orient] + (Reading() || n.orient == 0 || n.orient == 3 ? "" : " (khi đọc sách)"), 1500);
        break;
    }
    case KeyAction::Transfer: OpenTransfer(); break;
    case KeyAction::Assistant:   // only while the assistant is switched on (Cài đặt › Trợ lý AI)
        if (v.ai_enabled) OpenAssistant("", "", true);
        else ui::Toast("Trợ lý AI đang tắt. Bật trong Cài đặt › Trợ lý AI.", 1800);
        break;
    case KeyAction::ContinueReading: {
        std::string last = reader::LastBook();
        if (last.empty()) ui::Toast("Chưa có sách đang đọc.", 1200);
        else OpenReader(last);
        break;
    }
    default: break;
    }
}

void OnKey(HwKey key, Press press) {
    ui::NoteActivity();
    const auto& v = settings::Get();
    // Power: fixed. Click = standby / wake; holding 3 s powers off (firmware pulse on P13).
    if (key == HwKey::Power) {
        if (press == Press::Long) { platform::PowerOff(); return; }
        if (g_standby) CloseStandby();
        else ShowStandby();
        return;
    }
    if (press == Press::Up) {   // releases only matter for push-to-talk
        if (key == HwKey::Ai && v.ai_enabled) AssistantKeyUp();
        return;
    }
    if (WakeIfStandby()) return;   // any other key only wakes the device
    platform::Vibrate(v.haptic);
    using settings::KeyAction;
    switch (key) {
    case HwKey::VolUp:
    case HwKey::VolDown: {
        const bool up = key == HwKey::VolUp;
        if (VolumeTurnsPages()) {          // stock: Vol+ = previous page, Vol– = next page
            if (press != Press::Long || v.hold_to_flip) ui::InvokePageKey(up ? -1 : +1);
            return;
        }
        if (press == Press::Long) Volume(0, up ? 100 : 0);
        else Volume(up ? +10 : -10);
        return;
    }
    case HwKey::Ai:
        if (press == Press::Up) { if (v.ai_enabled) AssistantKeyUp(); return; }   // push-to-talk released
        RunKeyAction(v.keys[press == Press::Long ? settings::kAiLong : press == Press::Double ? settings::kAiDouble : settings::kAiClick]);
        return;
    case HwKey::Home:
        RunKeyAction(v.keys[press == Press::Long ? settings::kHomeLong : settings::kHomeClick]);
        return;
    case HwKey::Back:
        if (Reading()) { ui::InvokePageKey(-1); return; }          // fixed: previous page while reading
        if (press == Press::Long && v.keys[settings::kBackLong] != KeyAction::None) RunKeyAction(v.keys[settings::kBackLong]);
        else ui::InvokeBack();                                     // fixed: back
        return;
    case HwKey::Next:
        if (Reading()) { ui::InvokePageKey(+1); return; }          // fixed: next page while reading
        RunKeyAction(v.keys[press == Press::Long ? settings::kNextLong : settings::kNextClick]);
        return;
    default: return;
    }
}

void OnPowerKey() { OnKey(HwKey::Power, Press::Click); }
void OnAiKey(bool hold) { OnKey(HwKey::Ai, hold ? Press::Long : Press::Click); }

}  // namespace screens
