// "Nghe nhạc": MP3/WAV player for <sd>/music with a now-playing panel and a paged playlist.
#include <algorithm>
#include <cstdio>
#include <memory>

#include "media_info.h"
#include "platform.h"
#include "screens.h"
#include "icons.h"
#include "ui_common.h"

namespace screens {
namespace {

constexpr int kPerPage = 5;
constexpr uint32_t kTickMs = 1000;  // device: 5 s + partial refresh of the time label only

struct State {
    ui::Page page;
    std::vector<std::string> files;
    int current = -1;
    int list_page = 0;
    lv_obj_t* title = nullptr;
    lv_obj_t* artist = nullptr;
    lv_obj_t* bar = nullptr;
    lv_obj_t* pos = nullptr;
    lv_obj_t* dur = nullptr;
    lv_obj_t* play_icon = nullptr;   // ▶ glyph
    lv_obj_t* pause_bars = nullptr;  // ‖ drawn as two bars (crisper than the font glyph on e-ink)
    lv_obj_t* vol = nullptr;
    lv_obj_t* list = nullptr;
    lv_timer_t* timer = nullptr;
    platform::PlayState last_state = platform::PlayState::Stopped;
};

void RenderList(State* s);

std::string PathOf(State* s, int i) { return platform::MusicDir() + "/" + s->files[i]; }

void Play(State* s, int i) {
    if (s->files.empty()) return;
    i = (i + (int)s->files.size()) % (int)s->files.size();
    s->current = i;
    if (!platform::AudioPlay(PathOf(s, i))) ui::Toast("Không phát được: " + platform::AudioStatus().error);
    media::Info info = media::Read(PathOf(s, i));
    lv_label_set_text(s->title, (info.title.empty() ? media::DisplayName(s->files[i]) : info.title).c_str());
    lv_label_set_text(s->artist, (info.artist.empty() ? "Không rõ nghệ sĩ" : info.artist).c_str());
    s->list_page = i / kPerPage;
    RenderList(s);
}

void Tick(State* s) {
    platform::PlayStatus st = platform::AudioStatus();
    bool playing = st.state == platform::PlayState::Playing || st.state == platform::PlayState::Loading;
    if (playing) { lv_obj_add_flag(s->play_icon, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(s->pause_bars, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_remove_flag(s->play_icon, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s->pause_bars, LV_OBJ_FLAG_HIDDEN); }
    lv_label_set_text(s->pos, ui::FormatTime(st.position_ms).c_str());
    lv_label_set_text(s->dur, st.duration_ms ? ui::FormatTime(st.duration_ms).c_str() : "--:--");
    int pct = st.duration_ms ? (int)(st.position_ms * 100 / st.duration_ms) : 0;
    lv_obj_t* fill = lv_obj_get_child(s->bar, 0);
    lv_obj_set_width(fill, LV_PCT(std::max(1, pct)));
    if (pct > 0) lv_obj_remove_flag(fill, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s->vol, "Âm lượng %d%%", st.volume);
    if (st.state == platform::PlayState::Ended && s->last_state != platform::PlayState::Ended) Play(s, s->current + 1);
    s->last_state = st.state;
}

void RenderList(State* s) {
    lv_obj_clean(s->list);
    int pages = std::max(1, ((int)s->files.size() + kPerPage - 1) / kPerPage);
    s->list_page = std::clamp(s->list_page, 0, pages - 1);
    if (s->files.empty()) {
        lv_obj_t* l = ui::Label(s->list, "Chưa có bài hát. Chép MP3 vào thư mục music/ trên thẻ nhớ hoặc tải từ Thư viện Drive.", ui::F().body);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        return;
    }
    for (int i = s->list_page * kPerPage; i < (int)s->files.size() && i < (s->list_page + 1) * kPerPage; ++i) {
        lv_obj_t* row = ui::Button(s->list, "", [s, i] { Play(s, i); }, LV_PCT(100), 78);
        lv_obj_set_style_pad_hor(row, 16, 0);
        char num[16];
        std::snprintf(num, sizeof num, "%02d", i + 1);
        lv_obj_align(ui::Label(row, num, ui::F().small), LV_ALIGN_LEFT_MID, 0, 0);
        media::Info info = media::Read(PathOf(s, i));
        lv_obj_t* t = ui::Label(row, info.title.empty() ? media::DisplayName(s->files[i]) : info.title, ui::F().body);
        lv_obj_set_size(t, 470, lv_font_get_line_height(ui::F().body));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 50, 0);
        if (i == s->current) lv_obj_align(ui::IconLabel(row, icon::MUSIC), LV_ALIGN_RIGHT_MID, 0, 0);
        ui::Invert(row, i == s->current);
    }
    lv_obj_t* nav = lv_obj_create(s->list);
    lv_obj_set_size(nav, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    if (s->list_page > 0) lv_obj_align(ui::Button(nav, "‹ Trước", [s] { s->list_page--; RenderList(s); }, 190, 70), LV_ALIGN_LEFT_MID, 0, 0);
    char pg[48];
    std::snprintf(pg, sizeof pg, "%d / %d · %d bài", s->list_page + 1, pages, (int)s->files.size());
    lv_obj_center(ui::Label(nav, pg, ui::F().small));
    if (s->list_page + 1 < pages) lv_obj_align(ui::Button(nav, "Sau ›", [s] { s->list_page++; RenderList(s); }, 190, 70), LV_ALIGN_RIGHT_MID, 0, 0);
}

lv_obj_t* RoundButton(lv_obj_t* parent, const char* symbol, int size, bool primary, ui::Action a, lv_obj_t** icon_out = nullptr) {
    lv_obj_t* b = ui::Button(parent, "", std::move(a), size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_t* i = ui::IconLabel(b, symbol, primary ? ui::F().icon_big : ui::F().icon);
    lv_obj_center(i);
    ui::Invert(b, primary);
    if (icon_out) *icon_out = i;
    return b;
}

}  // namespace

void OpenMusic() {
    auto* s = new State();
    s->page = ui::NewPage("NGHE NHẠC", [] { OpenApps(); });
    lv_obj_add_event_cb(s->page.screen, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        lv_timer_delete(st->timer);
        delete st;  // playback keeps running in the background, like a real player
    }, LV_EVENT_DELETE, s);
    s->files = platform::ListFiles(platform::MusicDir(), {".mp3", ".wav"});
    lv_label_set_text(s->page.header_status, "Thư mục music/ trên thẻ nhớ · MP3, WAV");
    lv_obj_t* body = s->page.body;

    // Now playing panel
    lv_obj_t* np = lv_obj_create(body);
    lv_obj_set_size(np, LV_PCT(100), 470);
    lv_obj_set_style_border_width(np, 3, 0);
    lv_obj_set_style_border_color(np, lv_color_black(), 0);
    lv_obj_set_style_radius(np, 12, 0);
    lv_obj_set_style_pad_all(np, 20, 0);
    lv_obj_remove_flag(np, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* art = lv_obj_create(np);
    lv_obj_remove_flag(art, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(art, 130, 130);
    lv_obj_set_style_radius(art, 10, 0);
    lv_obj_set_style_bg_color(art, lv_color_black(), 0);
    lv_obj_set_style_border_width(art, 0, 0);
    lv_obj_t* note = ui::IconLabel(art, icon::MUSIC, ui::F().icon_big);
    lv_obj_set_style_text_color(note, lv_color_white(), 0);
    lv_obj_center(note);
    s->title = ui::Label(np, "Chưa phát bài nào", ui::F().title);
    lv_obj_set_width(s->title, 450);
    lv_obj_set_height(s->title, lv_font_get_line_height(ui::F().title) * 2);
    lv_label_set_long_mode(s->title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s->title, LV_ALIGN_TOP_LEFT, 150, 0);
    s->artist = ui::Label(np, "Chọn một bài trong danh sách", ui::F().body);
    lv_obj_set_size(s->artist, 450, lv_font_get_line_height(ui::F().body));
    lv_label_set_long_mode(s->artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s->artist, LV_ALIGN_TOP_LEFT, 150, 100);

    s->bar = ui::Progress(np, 0, 600, 12);
    lv_obj_align(s->bar, LV_ALIGN_TOP_LEFT, 0, 162);
    s->pos = ui::Label(np, "0:00", ui::F().small);
    lv_obj_align(s->pos, LV_ALIGN_TOP_LEFT, 0, 184);
    s->dur = ui::Label(np, "--:--", ui::F().small);
    lv_obj_align(s->dur, LV_ALIGN_TOP_RIGHT, 0, 184);

    lv_obj_t* ctl = lv_obj_create(np);
    lv_obj_set_size(ctl, LV_PCT(100), 130);
    lv_obj_align(ctl, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_style_border_width(ctl, 0, 0);
    lv_obj_set_style_pad_all(ctl, 0, 0);
    lv_obj_set_flex_flow(ctl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctl, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctl, 40, 0);
    lv_obj_remove_flag(ctl, LV_OBJ_FLAG_SCROLLABLE);
    RoundButton(ctl, icon::PREV, 96, false, [s] { Play(s, s->current < 0 ? 0 : s->current - 1); });
    lv_obj_t* play = RoundButton(ctl, icon::PLAY, 124, true, [s] {
        auto st = platform::AudioStatus();
        if (st.state == platform::PlayState::Playing) platform::AudioPause(true);
        else if (st.state == platform::PlayState::Paused) platform::AudioPause(false);
        else Play(s, s->current < 0 ? 0 : s->current);
        Tick(s);
    }, &s->play_icon);
    s->pause_bars = lv_obj_create(play);
    lv_obj_remove_flag(s->pause_bars, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(s->pause_bars, 38, 44);
    lv_obj_set_style_bg_opa(s->pause_bars, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s->pause_bars, 0, 0);
    lv_obj_set_style_pad_all(s->pause_bars, 0, 0);
    lv_obj_center(s->pause_bars);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t* bar = lv_obj_create(s->pause_bars);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(bar, 13, 44);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
        lv_obj_align(bar, i ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);
    }
    RoundButton(ctl, icon::NEXT, 96, false, [s] { Play(s, s->current + 1); });

    lv_obj_t* volrow = lv_obj_create(np);
    lv_obj_set_size(volrow, LV_PCT(100), 72);
    lv_obj_align(volrow, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(volrow, 0, 0);
    lv_obj_set_style_pad_all(volrow, 0, 0);
    lv_obj_remove_flag(volrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ui::IconLabel(volrow, icon::VOLUME), LV_ALIGN_LEFT_MID, 0, 0);
    s->vol = ui::Label(volrow, "", ui::F().body);
    lv_obj_align(s->vol, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_align(ui::Button(volrow, "-", [s] { platform::AudioSetVolume(platform::AudioStatus().volume - 10); Tick(s); }, 90, 66),
                 LV_ALIGN_RIGHT_MID, -104, 0);
    lv_obj_align(ui::Button(volrow, "+", [s] { platform::AudioSetVolume(platform::AudioStatus().volume + 10); Tick(s); }, 90, 66),
                 LV_ALIGN_RIGHT_MID, 0, 0);

    ui::Label(body, "Danh sách phát", ui::F().body_bold);
    s->list = lv_obj_create(body);
    lv_obj_set_size(s->list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(s->list, 0, 0);
    lv_obj_set_style_pad_all(s->list, 0, 0);
    lv_obj_set_style_pad_row(s->list, 10, 0);
    lv_obj_set_flex_flow(s->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s->list, LV_OBJ_FLAG_SCROLLABLE);

    // Resume the panel if something is already playing.
    platform::PlayStatus st = platform::AudioStatus();
    for (int i = 0; i < (int)s->files.size(); ++i)
        if (PathOf(s, i) == st.path) {
            s->current = i;
            media::Info info = media::Read(st.path);
            lv_label_set_text(s->title, (info.title.empty() ? media::DisplayName(s->files[i]) : info.title).c_str());
            lv_label_set_text(s->artist, (info.artist.empty() ? "Không rõ nghệ sĩ" : info.artist).c_str());
            s->list_page = i / kPerPage;
        }
    s->last_state = st.state;
    RenderList(s);
    Tick(s);
    s->timer = lv_timer_create([](lv_timer_t* t) { Tick(static_cast<State*>(lv_timer_get_user_data(t))); }, kTickMs, s);
    ui::SetPageKeyHandler([s](int dir) { Play(s, s->current + dir); });   // page keys = previous/next track
    ui::Show(s->page.screen);
}

}  // namespace screens
