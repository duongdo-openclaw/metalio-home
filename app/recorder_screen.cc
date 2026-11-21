// "Ghi âm": 16 kHz mono WAV recorder (same format as the firmware's app recorder) with a list
// of saved recordings that can be played back or deleted.
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>

#include "media_info.h"
#include "platform.h"
#include "screens.h"
#include "icons.h"
#include "ui_common.h"

namespace screens {
namespace {

constexpr int kSegments = 24;     // level meter: discrete blocks read better on e-ink than a smooth bar
constexpr int kPerPage = 4;
constexpr uint32_t kTickMs = 250;

struct State {
    ui::Page page;
    lv_obj_t* time = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* meter = nullptr;
    lv_obj_t* rec_btn = nullptr;
    lv_obj_t* rec_label = nullptr;
    lv_obj_t* pause_btn = nullptr;
    lv_obj_t* pause_label = nullptr;
    lv_obj_t* list = nullptr;
    lv_timer_t* timer = nullptr;
    std::vector<std::string> files;
    int list_page = 0;
    std::string confirm_delete;
    std::string playing;
};

void RenderList(State* s);

std::string NewName() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof b, "GhiAm_%Y%m%d_%H%M%S.wav", std::localtime(&t));
    return b;
}

std::string Pretty(const std::string& name) {  // GhiAm_20261120_231502.wav -> 20/11/2026 23:15:02
    int y, mo, d, h, mi, se;
    if (std::sscanf(name.c_str(), "GhiAm_%4d%2d%2d_%2d%2d%2d", &y, &mo, &d, &h, &mi, &se) == 6) {
        char b[48];
        std::snprintf(b, sizeof b, "%02d/%02d/%04d  %02d:%02d:%02d", d, mo, y, h, mi, se);
        return b;
    }
    return media::DisplayName(name);
}

void Refresh(State* s) {
    platform::RecStatus r = platform::RecGetStatus();
    const bool active = r.state == platform::RecState::Recording || r.state == platform::RecState::Paused;
    std::string t = ui::FormatTime(r.duration_ms);
    if (t.size() < 5) t = "0" + t;
    lv_label_set_text(s->time, t.c_str());
    const char* st = r.state == platform::RecState::Recording ? "Đang ghi…"
                     : r.state == platform::RecState::Paused  ? "Tạm dừng"
                     : r.state == platform::RecState::Error   ? "Lỗi"
                                                              : "Sẵn sàng · WAV 16 kHz mono";
    lv_label_set_text(s->status, r.state == platform::RecState::Error ? ("Lỗi: " + r.error).c_str() : st);
    int lit = r.state == platform::RecState::Recording ? r.level * kSegments / 100 : 0;
    for (int i = 0; i < kSegments; ++i) {
        lv_obj_t* seg = lv_obj_get_child(s->meter, i);
        lv_obj_set_style_bg_color(seg, i < lit ? lv_color_black() : lv_color_white(), 0);
    }
    lv_label_set_text(s->rec_label, active ? "Lưu" : "Ghi");
    lv_label_set_text(s->pause_label, r.state == platform::RecState::Paused ? "Tiếp tục" : "Tạm dừng");
    if (active) lv_obj_remove_state(s->pause_btn, LV_STATE_DISABLED);
    else lv_obj_add_state(s->pause_btn, LV_STATE_DISABLED);
    if (!s->playing.empty() && platform::AudioStatus().state != platform::PlayState::Playing) {
        s->playing.clear();
        RenderList(s);
    }
}

void RecordOrSave(State* s) {
    platform::RecStatus r = platform::RecGetStatus();
    if (r.state == platform::RecState::Recording || r.state == platform::RecState::Paused) {
        if (platform::RecStop()) ui::Toast("Đã lưu bản ghi " + ui::FormatTime(r.duration_ms), 1800);
        else ui::Toast("Lưu lỗi: " + platform::RecGetStatus().error);
        s->files = platform::ListFiles(platform::RecordingsDir(), {".wav"});
        std::reverse(s->files.begin(), s->files.end());  // newest first
        s->list_page = 0;
        RenderList(s);
    } else {
        platform::AudioStop();  // the speaker would be recorded too
        s->playing.clear();
        if (!platform::RecStart(platform::RecordingsDir() + "/" + NewName()))
            ui::Toast("Không ghi âm được: " + platform::RecGetStatus().error);
    }
    Refresh(s);
}

void RenderList(State* s) {
    lv_obj_clean(s->list);
    if (s->files.empty()) {
        ui::Label(s->list, "Chưa có bản ghi nào.", ui::F().body);
        return;
    }
    int pages = std::max(1, ((int)s->files.size() + kPerPage - 1) / kPerPage);
    s->list_page = std::clamp(s->list_page, 0, pages - 1);
    for (int i = s->list_page * kPerPage; i < (int)s->files.size() && i < (s->list_page + 1) * kPerPage; ++i) {
        std::string name = s->files[i];
        std::string path = platform::RecordingsDir() + "/" + name;
        bool playing = s->playing == path;
        lv_obj_t* row = ui::Button(s->list, "", [s, path] {
            if (s->playing == path) { platform::AudioStop(); s->playing.clear(); }
            else if (platform::AudioPlay(path)) s->playing = path;
            RenderList(s);
        }, LV_PCT(100), 96);
        lv_obj_set_style_pad_hor(row, 16, 0);
        lv_obj_align(ui::IconLabel(row, playing ? icon::STOP : icon::PLAY), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(ui::Label(row, Pretty(name), ui::F().body_bold), LV_ALIGN_TOP_LEFT, 52, 4);
        media::Info info = media::Read(path);
        char sub[64];
        std::snprintf(sub, sizeof sub, "%s · %llu KB", ui::FormatTime(info.duration_ms).c_str(),
                      (unsigned long long)(platform::FileSize(path) / 1024));
        lv_obj_align(ui::Label(row, sub, ui::F().small), LV_ALIGN_BOTTOM_LEFT, 52, -4);
        bool confirming = s->confirm_delete == path;
        lv_obj_t* del = ui::Button(row, confirming ? "Chắc chắn?" : "Xoá", [s, path] {
            if (s->confirm_delete != path) { s->confirm_delete = path; RenderList(s); return; }
            if (s->playing == path) { platform::AudioStop(); s->playing.clear(); }
            platform::RemoveFile(path);
            s->confirm_delete.clear();
            s->files.erase(std::remove(s->files.begin(), s->files.end(), path.substr(path.rfind('/') + 1)), s->files.end());
            RenderList(s);
        }, confirming ? 170 : 100, 64);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
        ui::Invert(row, playing);
        ui::Invert(del, confirming);   // Invert(row) recolours the button's text too; restore it
    }
    lv_obj_t* nav = lv_obj_create(s->list);
    lv_obj_set_size(nav, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    if (s->list_page > 0) lv_obj_align(ui::Button(nav, "‹ Trước", [s] { s->list_page--; RenderList(s); }, 190, 70), LV_ALIGN_LEFT_MID, 0, 0);
    char pg[48];
    std::snprintf(pg, sizeof pg, "%d / %d · %d bản ghi", s->list_page + 1, pages, (int)s->files.size());
    lv_obj_center(ui::Label(nav, pg, ui::F().small));
    if (s->list_page + 1 < pages) lv_obj_align(ui::Button(nav, "Sau ›", [s] { s->list_page++; RenderList(s); }, 190, 70), LV_ALIGN_RIGHT_MID, 0, 0);
}

}  // namespace

void OpenRecorder() {
    auto* s = new State();
    s->page = ui::NewPage("GHI ÂM", [] { OpenApps(); });
    lv_obj_add_event_cb(s->page.screen, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        lv_timer_delete(st->timer);
        delete st;
    }, LV_EVENT_DELETE, s);
    lv_label_set_text(s->page.header_status, "Lưu vào thư mục recordings/ trên thẻ nhớ");
    lv_obj_t* body = s->page.body;

    lv_obj_t* panel = lv_obj_create(body);
    lv_obj_set_size(panel, LV_PCT(100), 470);
    lv_obj_set_style_border_width(panel, 3, 0);
    lv_obj_set_style_border_color(panel, lv_color_black(), 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    s->time = ui::Label(panel, "00:00", ui::F().reader(96, true));
    lv_obj_align(s->time, LV_ALIGN_TOP_MID, 0, -6);
    s->status = ui::Label(panel, "", ui::F().body);
    lv_obj_align(s->status, LV_ALIGN_TOP_MID, 0, 132);

    s->meter = lv_obj_create(panel);
    lv_obj_set_size(s->meter, LV_PCT(100), 52);
    lv_obj_align(s->meter, LV_ALIGN_TOP_MID, 0, 184);
    lv_obj_set_style_border_width(s->meter, 0, 0);
    lv_obj_set_style_pad_all(s->meter, 0, 0);
    lv_obj_set_style_pad_column(s->meter, 5, 0);
    lv_obj_set_flex_flow(s->meter, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(s->meter, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kSegments; ++i) {
        lv_obj_t* seg = lv_obj_create(s->meter);
        lv_obj_remove_flag(seg, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_size(seg, 19, 16 + i * 36 / kSegments);
        lv_obj_set_align(seg, LV_ALIGN_BOTTOM_LEFT);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_set_style_border_width(seg, 2, 0);
        lv_obj_set_style_border_color(seg, lv_color_black(), 0);
    }
    lv_obj_set_flex_align(s->meter, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    lv_obj_t* ctl = lv_obj_create(panel);
    lv_obj_set_size(ctl, LV_PCT(100), 150);
    lv_obj_align(ctl, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(ctl, 0, 0);
    lv_obj_set_style_pad_all(ctl, 0, 0);
    lv_obj_remove_flag(ctl, LV_OBJ_FLAG_SCROLLABLE);
    s->rec_btn = ui::Button(ctl, "", [s] { RecordOrSave(s); }, 150, 150);
    lv_obj_set_style_radius(s->rec_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_t* dot = lv_obj_create(s->rec_btn);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dot, 44, 44);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, -18);
    s->rec_label = ui::Label(s->rec_btn, "Ghi", ui::F().body_bold);
    lv_obj_align(s->rec_label, LV_ALIGN_CENTER, 0, 34);
    ui::Invert(s->rec_btn, true);
    lv_obj_align(s->rec_btn, LV_ALIGN_CENTER, 0, 0);
    s->pause_btn = ui::Button(ctl, "", [s] {
        auto r = platform::RecGetStatus();
        if (r.state == platform::RecState::Recording) platform::RecPause(true);
        else if (r.state == platform::RecState::Paused) platform::RecPause(false);
        Refresh(s);
    }, 170, 80);
    s->pause_label = ui::Label(s->pause_btn, "Tạm dừng", ui::F().body_bold);
    lv_obj_center(s->pause_label);
    lv_obj_set_style_opa(s->pause_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_align(s->pause_btn, LV_ALIGN_RIGHT_MID, 0, 0);

    ui::Label(body, "Bản ghi đã lưu", ui::F().body_bold);
    s->list = lv_obj_create(body);
    lv_obj_set_size(s->list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(s->list, 0, 0);
    lv_obj_set_style_pad_all(s->list, 0, 0);
    lv_obj_set_style_pad_row(s->list, 10, 0);
    lv_obj_set_flex_flow(s->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s->list, LV_OBJ_FLAG_SCROLLABLE);
    s->files = platform::ListFiles(platform::RecordingsDir(), {".wav"});
    std::reverse(s->files.begin(), s->files.end());
    RenderList(s);
    Refresh(s);
    s->timer = lv_timer_create([](lv_timer_t* t) { Refresh(static_cast<State*>(lv_timer_get_user_data(t))); }, kTickMs, s);
    ui::Show(s->page.screen);
}

}  // namespace screens
