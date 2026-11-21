// Book reader, modelled on Kreader / Kindle / Kobo:
//   own line layout (justified text, indent, line height %, letter spacing, margins, fonts),
//   status lines (chapter, clock, page, time left in chapter, %), tap zones (left-handed option),
//   corner bookmark, bookmarks list, accent-insensitive search, table of contents,
//   auto page turn, live "Aa" display sheet, reading statistics and periodic full refresh.
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>

#include "book.h"
#include "dictionary.h"
#include "notes.h"
#include "paginate.h"
#include "platform.h"
#include "reading_state.h"
#include "screens.h"
#include "settings.h"
#include "text_search.h"
#include "icons.h"
#include "ui_common.h"
#include "vocab.h"

namespace screens {
namespace {

constexpr int kHeaderH = 54, kFooterH = 62;
const lv_color_t kInk = lv_color_hex(0x111110);
const lv_color_t kMuted = lv_color_hex(0x5f5d58);

struct State {
    book::Book book;
    std::vector<book::Paragraph> paras;
    std::vector<reader::Page> pages;
    reader::Settings set;
    reader::Layout layout;
    int chapter = 0, page = 0;
    lv_obj_t* screen = nullptr;
    lv_obj_t* page_obj = nullptr;
    lv_obj_t* head_left = nullptr;
    lv_obj_t* head_right = nullptr;   // clock, centred
    lv_obj_t* battery = nullptr;      // ring only, no number while reading
    lv_obj_t* foot_left = nullptr;
    lv_obj_t* foot_mid = nullptr;
    lv_obj_t* foot_right = nullptr;
    lv_obj_t* foot_track = nullptr;
    lv_obj_t* foot_fill = nullptr;
    lv_obj_t* ribbon = nullptr;
    lv_obj_t* overlay = nullptr;     // menu / sheets; nullptr when reading
    lv_timer_t* clock = nullptr;
    lv_timer_t* auto_turn = nullptr;
    uint32_t page_shown_ms = 0;
    double sec_per_page = 45;        // reading speed estimate, refined as the reader turns pages
    int turns = 0;
    std::vector<reader::Bookmark> marks;
    std::shared_ptr<bool> alive;
    std::string query;
    struct Hit { int chapter, para; size_t offset; std::string snippet; };
    std::vector<Hit> hits;
    int hit_page = 0;
    int toc_page = 0;
    // Selection (long-press a word, tap another word to extend) and saved highlights.
    bool selecting = false;
    bool swallow_click = false;      // the release after a long press must not turn the page
    int sel_pa = 0, sel_pb = 0;      // anchor word / end word (paragraph, byte range)
    size_t sel_a0 = 0, sel_a1 = 0, sel_b0 = 0, sel_b1 = 0;
    lv_obj_t* selbar = nullptr;
    std::vector<notes::Note> notes;  // this book's notes (highlights drawn on the page)
};

void ShowPage(State* s);
void CloseOverlay(State* s);
void OpenMenu(State* s);
void OpenDisplaySheet(State* s, int tab);
void OpenSearch(State* s);
void ClearSelection(State* s);

lv_obj_t* Plain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

const lv_font_t* Face(const reader::Settings& st, int px, bool bold) {
    const lv_font_t* f = ui::F().face ? ui::F().face(st.font, px, bold) : nullptr;
    return f ? f : ui::F().reader(px, bold);
}

// ---------- layout ----------

void ComputeLayout(State* s) {
    const auto& st = s->set;
    reader::Layout& l = s->layout;
    l.body = Face(st, st.font_px, st.bold);
    l.heading = Face(st, st.font_px * 5 / 4, true);
    const int fh = lv_font_get_line_height(l.body);
    l.line_h = std::max(fh, st.font_px * st.line_pct / 100);
    l.letter = st.letter;
    l.indent = st.indent ? st.font_px * 3 / 2 : 0;
    l.para_gap = st.indent ? 0 : l.line_h / 2;
    l.justify = st.justify;
    const int top = (st.status ? kHeaderH : 18) + st.margin_v;
    const int bottom = (st.status ? kFooterH : 18) + st.margin_v;
    l.width = ui::ScreenW() - 2 * st.margin_h;
    l.height = ui::ScreenH() - top - bottom;
    lv_obj_set_pos(s->page_obj, st.margin_h, top);
    lv_obj_set_size(s->page_obj, l.width, l.height);
}

int BookPercent(State* s) {
    int n = s->book.ChapterCount();
    if (n == 0) return 0;
    double within = s->pages.empty() ? 0 : (double)(s->page + 1) / s->pages.size();
    return (int)std::min(100.0, (s->chapter + within) * 100.0 / n);
}

void PagePos(State* s, int& para, size_t& offset) {
    para = 0;
    offset = 0;
    if (!s->pages.empty() && !s->pages[s->page].lines.empty()) {
        para = s->pages[s->page].lines.front().para;
        offset = s->pages[s->page].lines.front().from;
    }
}

void SaveProgress(State* s) {
    reader::Progress p;
    p.chapter = s->chapter;
    PagePos(s, p.para, p.offset);
    p.percent = BookPercent(s);
    reader::SaveProgress(s->book.Path(), p);
}

bool LoadChapter(State* s, int chapter, int para, size_t offset, bool at_end = false) {
    std::string err;
    std::vector<book::Paragraph> paras;
    if (!s->book.LoadChapter(chapter, paras, err)) { ui::Toast(err); return false; }
    s->chapter = chapter;
    s->paras = std::move(paras);
    s->pages = reader::Paginate(s->paras, s->layout);
    s->page = at_end ? (int)s->pages.size() - 1 : reader::FindPage(s->pages, para, offset);
    return true;
}

void Relayout(State* s) {
    int para;
    size_t off;
    PagePos(s, para, off);
    reader::SaveSettings(s->set);
    ComputeLayout(s);
    s->pages = reader::Paginate(s->paras, s->layout);
    s->page = reader::FindPage(s->pages, para, off);
    ShowPage(s);
}

// ---------- page turning ----------

void CountPage(State* s) {
    uint32_t now = lv_tick_get();
    if (s->page_shown_ms) {
        uint32_t secs = std::min<uint32_t>((now - s->page_shown_ms) / 1000, 300);
        reader::AddReading(1, (int)secs);
        if (secs >= 3 && secs < 300) s->sec_per_page = s->sec_per_page * 0.7 + secs * 0.3;
    }
    s->page_shown_ms = now;
}

bool Turn(State* s, int dir) {
    if (s->overlay) { CloseOverlay(s); return true; }
    if (s->selecting) ClearSelection(s);
    if (dir > 0) CountPage(s);
    const int every = settings::Get().full_refresh_pages;   // clear e-ink ghosting periodically
    if (every > 0 && ++s->turns % every == 0) platform::FullRefresh();
    int n = s->page + dir;
    if (n >= 0 && n < (int)s->pages.size()) { s->page = n; ShowPage(s); return true; }
    // Look for the next chapter that has text (skips covers, blank title pages …).
    const int here_ch = s->chapter;
    int here_para;
    size_t here_off;
    PagePos(s, here_para, here_off);
    int ch = s->chapter + dir;
    while (ch >= 0 && ch < s->book.ChapterCount()) {
        if (LoadChapter(s, ch, 0, 0, dir < 0) && !s->paras.empty()) { ShowPage(s); return true; }
        ch += dir;
    }
    // Nothing there: stay exactly where the reader was (the loop replaced the loaded chapter).
    LoadChapter(s, here_ch, here_para, here_off);
    ShowPage(s);
    ui::Toast(dir > 0 ? "Đã hết sách." : "Đang ở trang đầu.", 1500);
    return false;
}

void StopAutoTurn(State* s) {
    if (s->auto_turn) { lv_timer_delete(s->auto_turn); s->auto_turn = nullptr; }
}

void StartAutoTurn(State* s) {
    StopAutoTurn(s);
    s->auto_turn = lv_timer_create([](lv_timer_t* t) {
        auto* st = static_cast<State*>(lv_timer_get_user_data(t));
        ui::NoteActivity();                       // auto-reading keeps the device awake
        if (!Turn(st, +1)) { StopAutoTurn(st); ShowPage(st); ui::Toast("Đã tới cuối sách: dừng tự lật trang", 2000); }
    }, (uint32_t)s->set.auto_turn * 1000, s);
    ShowPage(s);
}

// ---------- bookmarks ----------

bool InPage(State* s, const reader::Bookmark& b) {
    if (s->pages.empty() || s->pages[s->page].lines.empty() || b.chapter != s->chapter) return false;
    const auto& first = s->pages[s->page].lines.front();
    const auto& last = s->pages[s->page].lines.back();
    bool after_start = b.para > first.para || (b.para == first.para && b.offset >= first.from);
    bool before_end = b.para < last.para || (b.para == last.para && b.offset <= last.to);
    return after_start && before_end;
}

bool PageBookmarked(State* s) {
    for (const auto& b : s->marks) if (InPage(s, b)) return true;
    return false;
}

void ToggleBookmark(State* s) {
    if (PageBookmarked(s)) {
        s->marks.erase(std::remove_if(s->marks.begin(), s->marks.end(), [s](const reader::Bookmark& b) { return InPage(s, b); }),
                       s->marks.end());
        ui::Toast("Đã bỏ dấu trang", 1200);
    } else {
        reader::Bookmark b;
        b.chapter = s->chapter;
        PagePos(s, b.para, b.offset);
        b.snippet = reader::PageText(s->pages[s->page], s->paras, 120);
        b.created = (long long)std::time(nullptr);
        s->marks.push_back(b);
        std::sort(s->marks.begin(), s->marks.end(), [](const auto& a, const auto& c) {
            return a.chapter != c.chapter ? a.chapter < c.chapter : a.para != c.para ? a.para < c.para : a.offset < c.offset;
        });
        ui::Toast("Đã thêm dấu trang", 1200);
    }
    reader::SaveBookmarks(s->book.Path(), s->marks);
    platform::Vibrate(settings::Get().haptic);
    ShowPage(s);
}

// ---------- page + status lines ----------

std::string Clock() {
    std::time_t t = std::time(nullptr);
    char b[16];
    std::strftime(b, sizeof b, "%H:%M", std::localtime(&t));
    return b;
}

void ShowPage(State* s) {
    lv_obj_invalidate(s->page_obj);
    const bool st = s->set.status;
    for (lv_obj_t* o : {s->head_left, s->head_right, s->battery, s->foot_left, s->foot_mid, s->foot_right, s->foot_track, s->foot_fill})
        st ? lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (st) {
        lv_label_set_text(s->head_left, s->book.ChapterTitle(s->chapter).c_str());
        lv_label_set_text(s->head_right, Clock().c_str());
        ui::SetBattery(s->battery, platform::BatteryPercent(), platform::Charging());
        lv_obj_align(s->battery, LV_ALIGN_TOP_RIGHT, -30, 30 - 11);
        char a[48];
        std::snprintf(a, sizeof a, "Trang %d / %d", s->page + 1, (int)s->pages.size());
        lv_label_set_text(s->foot_left, a);
        int left_pages = (int)s->pages.size() - s->page - 1;
        int mins = (int)(left_pages * s->sec_per_page / 60 + 0.5);
        std::string mid = s->auto_turn ? "Tự lật · " + std::to_string(s->set.auto_turn) + " giây"
                        : left_pages == 0 ? "Trang cuối chương"
                        : mins < 1 ? "Còn dưới 1 phút trong chương"
                        : "Còn ~" + std::to_string(mins) + " phút trong chương";
        lv_label_set_text(s->foot_mid, mid.c_str());
        int pct = BookPercent(s);
        lv_label_set_text_fmt(s->foot_right, "%d%%", pct);
        lv_obj_set_width(s->foot_fill, std::max(2, (ui::ScreenW() - 60) * pct / 100));
    }
    PageBookmarked(s) ? lv_obj_remove_flag(s->ribbon, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(s->ribbon, LV_OBJ_FLAG_HIDDEN);
    SaveProgress(s);
}

// ---------- overlays ----------

void CloseOverlay(State* s) {
    if (s->overlay) { lv_obj_delete_async(s->overlay); s->overlay = nullptr; }
    ui::SetBackHandler([] { OpenLibrary(); });
}

lv_obj_t* NewOverlay(State* s, bool transparent) {
    if (s->overlay) lv_obj_delete_async(s->overlay);
    s->overlay = Plain(s->screen);
    lv_obj_set_size(s->overlay, ui::ScreenW(), ui::ScreenH());
    lv_obj_add_flag(s->overlay, LV_OBJ_FLAG_CLICKABLE);
    if (!transparent) {
        lv_obj_set_style_bg_opa(s->overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s->overlay, lv_color_white(), 0);
    }
    lv_obj_add_event_cb(s->overlay, [](lv_event_t* e) {   // a tap outside the panels closes the overlay
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) CloseOverlay(static_cast<State*>(lv_event_get_user_data(e)));
    }, LV_EVENT_CLICKED, s);
    auto alive = s->alive;
    ui::SetBackHandler([s, alive] { if (*alive) CloseOverlay(s); });
    return s->overlay;
}

lv_obj_t* Panel(lv_obj_t* parent, int y, int h, bool top_border) {
    lv_obj_t* p = Plain(parent);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(p, ui::ScreenW(), h);
    lv_obj_set_pos(p, 0, y);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, lv_color_white(), 0);
    lv_obj_set_style_border_side(p, top_border ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(p, 3, 0);
    lv_obj_set_style_border_color(p, kInk, 0);
    lv_obj_set_style_pad_hor(p, 24, 0);
    lv_obj_set_style_pad_ver(p, 14, 0);
    return p;
}

lv_obj_t* Chip(lv_obj_t* parent, const std::string& text, bool on, ui::Action a, int w = LV_SIZE_CONTENT) {
    lv_obj_t* b = ui::Button(parent, text, std::move(a), w, 60);
    lv_obj_set_style_radius(b, 30, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_pad_hor(b, 20, 0);
    ui::Invert(b, on);
    return b;
}

lv_obj_t* Row(lv_obj_t* parent, const std::string& label) {
    lv_obj_t* r = Plain(parent);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(r, 10, 0);
    if (!label.empty()) {
        lv_obj_t* l = ui::Label(r, label, ui::F().small);
        lv_obj_set_style_text_color(l, kMuted, 0);
        lv_obj_set_width(l, 150);
    }
    return r;
}

// "Aa" display sheet: three tabs; every change re-lays out the page visible above the sheet.
void OpenDisplaySheet(State* s, int tab) {
    lv_obj_t* ov = NewOverlay(s, true);
    const int h = 560;
    lv_obj_t* p = Panel(ov, ui::ScreenH() - h, h, true);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_t* tabs = Row(p, "");
    lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char* names[] = {"Chữ", "Dòng & lề", "Đoạn & khác"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* b = ui::Button(tabs, names[i], [s, i] { OpenDisplaySheet(s, i); }, (ui::ScreenW() - 48 - 20) / 3, 56);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(b, i == tab ? 4 : 1, 0);
        lv_obj_set_style_radius(b, 0, 0);
    }
    auto apply = [s, tab](std::function<void(reader::Settings&)> f) {
        f(s->set);
        Relayout(s);
        OpenDisplaySheet(s, tab);
    };
    const auto& st = s->set;
    if (tab == 0) {
        lv_obj_t* r = Row(p, "Phông chữ");
        Chip(r, "Có chân", st.font == "serif", [apply] { apply([](reader::Settings& x) { x.font = "serif"; }); });
        Chip(r, "Không chân", st.font == "sans", [apply] { apply([](reader::Settings& x) { x.font = "sans"; }); });
        for (const auto& f : platform::ListFiles(platform::FontsDir(), {".ttf", ".otf"})) {
            std::string fam = "file:" + f;
            Chip(r, f.substr(0, f.rfind('.')), st.font == fam, [apply, fam] { apply([fam](reader::Settings& x) { x.font = fam; }); });
        }
        lv_obj_t* r2 = Row(p, "Cỡ chữ");
        Chip(r2, "A-", false, [apply] { apply([](reader::Settings& x) { x.font_px = std::max(22, x.font_px - 2); }); }, 96);
        lv_obj_t* v = ui::Label(r2, std::to_string(st.font_px) + " px", ui::F().title);
        lv_obj_set_width(v, 130);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
        Chip(r2, "A+", false, [apply] { apply([](reader::Settings& x) { x.font_px = std::min(64, x.font_px + 2); }); }, 96);
        lv_obj_t* r3 = Row(p, "Độ đậm");
        Chip(r3, "Thường", !st.bold, [apply] { apply([](reader::Settings& x) { x.bold = false; }); });
        Chip(r3, "Đậm", st.bold, [apply] { apply([](reader::Settings& x) { x.bold = true; }); });
        lv_obj_t* hint = ui::Label(p, "Thêm phông: chép tệp .ttf hoặc .otf vào thư mục fonts/ trên thẻ nhớ.", ui::F().small);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_color(hint, kMuted, 0);
    } else if (tab == 1) {
        lv_obj_t* r = Row(p, "Giãn dòng");
        for (int pct : {120, 140, 160, 180, 200})
            Chip(r, std::to_string(pct) + "%", st.line_pct == pct, [apply, pct] { apply([pct](reader::Settings& x) { x.line_pct = pct; }); });
        const char* mn[] = {"Hẹp", "Vừa", "Rộng"};
        lv_obj_t* r2 = Row(p, "Lề hai bên");
        const int mh[] = {20, 40, 64};
        for (int i = 0; i < 3; ++i) { int m = mh[i]; Chip(r2, mn[i], st.margin_h == m, [apply, m] { apply([m](reader::Settings& x) { x.margin_h = m; }); }); }
        lv_obj_t* r3 = Row(p, "Lề trên dưới");
        const int mv[] = {8, 24, 48};
        for (int i = 0; i < 3; ++i) { int m = mv[i]; Chip(r3, mn[i], st.margin_v == m, [apply, m] { apply([m](reader::Settings& x) { x.margin_v = m; }); }); }
        lv_obj_t* r4 = Row(p, "Căn chữ");
        Chip(r4, "Đều hai bên", st.justify, [apply] { apply([](reader::Settings& x) { x.justify = true; }); });
        Chip(r4, "Trái", !st.justify, [apply] { apply([](reader::Settings& x) { x.justify = false; }); });
        lv_obj_t* r5 = Row(p, "Cách chữ");
        Chip(r5, "-", false, [apply] { apply([](reader::Settings& x) { x.letter = std::max(-1, x.letter - 1); }); }, 80);
        lv_obj_t* v = ui::Label(r5, std::to_string(st.letter) + " px", ui::F().body_bold);
        lv_obj_set_width(v, 90);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
        Chip(r5, "+", false, [apply] { apply([](reader::Settings& x) { x.letter = std::min(4, x.letter + 1); }); }, 80);
    } else {
        lv_obj_t* r = Row(p, "Đoạn văn");
        Chip(r, "Thụt đầu dòng", st.indent, [apply] { apply([](reader::Settings& x) { x.indent = true; }); });
        Chip(r, "Cách đoạn", !st.indent, [apply] { apply([](reader::Settings& x) { x.indent = false; }); });
        lv_obj_t* r2 = Row(p, "Dòng trạng thái");
        Chip(r2, "Hiện", st.status, [apply] { apply([](reader::Settings& x) { x.status = true; }); });
        Chip(r2, "Ẩn", !st.status, [apply] { apply([](reader::Settings& x) { x.status = false; }); });
        lv_obj_t* r3 = Row(p, "Chạm để lật");
        Chip(r3, "Phải: trang sau", !st.left_hand, [apply] { apply([](reader::Settings& x) { x.left_hand = false; }); });
        Chip(r3, "Trái: trang sau", st.left_hand, [apply] { apply([](reader::Settings& x) { x.left_hand = true; }); });
        lv_obj_t* r4 = Row(p, "Tự lật mỗi");
        for (int sec : {15, 30, 60, 90}) {
            Chip(r4, std::to_string(sec) + " giây", st.auto_turn == sec, [s, apply, sec] {
                bool running = s->auto_turn != nullptr;
                apply([sec](reader::Settings& x) { x.auto_turn = sec; });
                if (running) StartAutoTurn(s);
            });
        }
    }
    lv_obj_t* close = ui::Button(p, "Xong", [s] { CloseOverlay(s); }, LV_PCT(100), 60);
    ui::Invert(close, true);
}

void JumpTo(State* s, int chapter, int para, size_t offset) {
    CloseOverlay(s);
    if (LoadChapter(s, chapter, para, offset)) ShowPage(s);
}

lv_obj_t* SheetPage(State* s, const char* title) {
    lv_obj_t* ov = NewOverlay(s, false);
    lv_obj_t* head = Plain(ov);
    lv_obj_set_size(head, ui::ScreenW(), 110);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(head, 3, 0);
    lv_obj_set_style_border_color(head, kInk, 0);
    lv_obj_set_style_pad_hor(head, ui::kPad, 0);
    lv_obj_align(ui::Button(head, std::string(icon::CHEV_L) + " Đóng", [s] { CloseOverlay(s); }, 140, 70), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(ui::Label(head, title, ui::F().title), LV_ALIGN_LEFT_MID, 160, 0);
    lv_obj_t* body = Plain(ov);
    lv_obj_set_size(body, ui::ScreenW(), ui::ScreenH() - 110);
    lv_obj_set_pos(body, 0, 110);
    lv_obj_set_style_pad_all(body, ui::kPad, 0);
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    return body;
}

void Pager(lv_obj_t* body, int page, int pages, std::function<void(int)> go) {
    lv_obj_t* nav = Plain(body);
    lv_obj_set_size(nav, LV_PCT(100), 76);
    if (page > 0) lv_obj_align(ui::Button(nav, "‹ Trước", [go, page] { go(page - 1); }, 190, 70), LV_ALIGN_LEFT_MID, 0, 0);
    char pg[32];
    std::snprintf(pg, sizeof pg, "%d / %d", page + 1, std::max(1, pages));
    lv_obj_center(ui::Label(nav, pg, ui::F().body));
    if (page + 1 < pages) lv_obj_align(ui::Button(nav, "Sau ›", [go, page] { go(page + 1); }, 190, 70), LV_ALIGN_RIGHT_MID, 0, 0);
}

int PerPage(int row_h) { return std::max(3, (ui::ScreenH() - 110 - 2 * ui::kPad - 90) / (row_h + 12)); }

void OpenToc(State* s) {
    lv_obj_t* body = SheetPage(s, "MỤC LỤC");
    const auto& toc = s->book.Toc();
    const int per = PerPage(80);
    if (toc.empty()) { ui::Label(body, "Sách không có mục lục.", ui::F().body); return; }
    int pages = ((int)toc.size() + per - 1) / per;
    int cur = 0;
    for (int i = 0; i < (int)toc.size(); ++i) if (toc[i].spine <= s->chapter) cur = i;
    if (s->toc_page < 0) s->toc_page = cur / per;
    s->toc_page = std::clamp(s->toc_page, 0, pages - 1);
    for (int i = s->toc_page * per; i < (int)toc.size() && i < (s->toc_page + 1) * per; ++i) {
        const auto& e = toc[i];
        int target = e.spine;
        lv_obj_t* b = ui::Button(body, "", [s, target] { JumpTo(s, target, 0, 0); }, LV_PCT(100), 80);
        lv_obj_t* l = ui::Label(b, e.title, ui::F().body);
        lv_obj_set_size(l, ui::ScreenW() - 2 * ui::kPad - 60 - e.depth * 28, lv_font_get_line_height(ui::F().body));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, e.depth * 28, 0);
        ui::Invert(b, i == cur);
    }
    Pager(body, s->toc_page, pages, [s](int p) { s->toc_page = p; OpenToc(s); });
}

void OpenBookmarks(State* s) {
    lv_obj_t* body = SheetPage(s, "DẤU TRANG");
    lv_obj_t* add = ui::Button(body, PageBookmarked(s) ? "Bỏ dấu trang này" : "Đánh dấu trang này", [s] {
        ToggleBookmark(s);
        OpenBookmarks(s);
    }, LV_PCT(100), 70);
    ui::Invert(add, true);
    if (s->marks.empty()) {
        lv_obj_t* l = ui::Label(body, "Chưa có dấu trang. Mẹo: chạm góc trên bên phải của trang để đánh dấu nhanh.", ui::F().body);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        return;
    }
    for (size_t i = 0; i < s->marks.size() && i < 7; ++i) {
        const auto b = s->marks[i];
        lv_obj_t* row = ui::Button(body, "", [s, b] { JumpTo(s, b.chapter, b.para, b.offset); }, LV_PCT(100), 112);
        lv_obj_set_style_pad_hor(row, 16, 0);
        const int w = ui::ScreenW() - 2 * ui::kPad - 160;
        lv_obj_t* t = ui::Label(row, s->book.ChapterTitle(b.chapter), ui::F().body_bold);
        lv_obj_set_size(t, w, lv_font_get_line_height(ui::F().body_bold));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 6);
        lv_obj_t* sn = ui::Label(row, b.snippet, ui::F().small);
        lv_obj_set_size(sn, w, lv_font_get_line_height(ui::F().small));
        lv_label_set_long_mode(sn, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(sn, LV_ALIGN_BOTTOM_LEFT, 0, -8);
        lv_obj_t* del = ui::Button(row, "Xoá", [s, i] {
            s->marks.erase(s->marks.begin() + (long)i);
            reader::SaveBookmarks(s->book.Path(), s->marks);
            ShowPage(s);
            OpenBookmarks(s);
        }, 100, 60);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

void RunSearch(State* s) {
    s->hits.clear();
    s->hit_page = 0;
    for (int ch = 0; ch < s->book.ChapterCount() && s->hits.size() < 200; ++ch) {
        std::vector<book::Paragraph> ps;
        std::string err;
        if (!s->book.LoadChapter(ch, ps, err)) continue;
        for (int pi = 0; pi < (int)ps.size() && s->hits.size() < 200; ++pi) {
            const std::string& t = ps[pi].text;
            for (size_t off : search::FindAll(t, s->query)) {
                size_t a = off > 40 ? off - 40 : 0;
                while (a > 0 && ((unsigned char)t[a] & 0xC0) == 0x80) --a;
                size_t e = std::min(t.size(), off + 80);
                while (e < t.size() && ((unsigned char)t[e] & 0xC0) == 0x80) ++e;
                s->hits.push_back({ch, pi, off, (a ? "…" : "") + t.substr(a, e - a) + (e < t.size() ? "…" : "")});
                if (s->hits.size() >= 200) break;
            }
        }
    }
}

void ShowResults(State* s) {
    lv_obj_t* body = SheetPage(s, "TÌM TRONG SÁCH");
    char head[160];
    std::snprintf(head, sizeof head, "%s%zu kết quả cho \"%s\"", s->hits.size() >= 200 ? "Hơn " : "", s->hits.size(), s->query.c_str());
    lv_obj_t* h = ui::Label(body, head, ui::F().body_bold);
    lv_obj_set_width(h, LV_PCT(100));
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_DOTS);
    const int per = PerPage(112) - 1;
    int pages = std::max(1, ((int)s->hits.size() + per - 1) / per);
    s->hit_page = std::clamp(s->hit_page, 0, pages - 1);
    for (int i = s->hit_page * per; i < (int)s->hits.size() && i < (s->hit_page + 1) * per; ++i) {
        const auto hit = s->hits[i];
        lv_obj_t* row = ui::Button(body, "", [s, hit] { JumpTo(s, hit.chapter, hit.para, hit.offset); }, LV_PCT(100), 112);
        lv_obj_set_style_pad_hor(row, 16, 0);
        lv_obj_t* t = ui::Label(row, s->book.ChapterTitle(hit.chapter), ui::F().small);
        lv_obj_set_size(t, LV_PCT(100), lv_font_get_line_height(ui::F().small));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 6);
        lv_obj_t* sn = ui::Label(row, hit.snippet, ui::F().body);
        lv_obj_set_size(sn, LV_PCT(100), lv_font_get_line_height(ui::F().body));
        lv_label_set_long_mode(sn, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(sn, LV_ALIGN_BOTTOM_LEFT, 0, -8);
    }
    Pager(body, s->hit_page, pages, [s](int p) { s->hit_page = p; ShowResults(s); });
    ui::Button(body, "Tìm từ khác", [s] { OpenSearch(s); }, LV_PCT(100), 70);
}

void DoSearch(State* s, lv_obj_t* ta) {
    s->query = lv_textarea_get_text(ta);
    if (s->query.empty()) return;
    RunSearch(s);
    ShowResults(s);
}

void OpenSearch(State* s) {
    lv_obj_t* body = SheetPage(s, "TÌM TRONG SÁCH");
    lv_obj_t* hint = ui::Label(body, "Gõ không dấu cũng được: \"khong dau\" tìm ra \"không dấu\".", ui::F().small);
    lv_obj_set_style_text_color(hint, kMuted, 0);
    lv_obj_t* ta = lv_textarea_create(body);
    lv_obj_set_size(ta, LV_PCT(100), 76);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_text(ta, s->query.c_str());
    lv_textarea_set_placeholder_text(ta, "Từ hoặc cụm từ cần tìm");
    lv_obj_set_style_text_font(ta, ui::F().body, 0);
    lv_obj_set_style_border_width(ta, 3, 0);
    lv_obj_set_style_border_color(ta, kInk, 0);
    lv_group_focus_obj(ta);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        DoSearch(static_cast<State*>(lv_event_get_user_data(e)), static_cast<lv_obj_t*>(lv_event_get_target(e)));
    }, LV_EVENT_READY, s);
    // On-screen keyboard: the device has no physical one; accent-free search makes ASCII enough.
    lv_obj_t* kb = lv_keyboard_create(body);
    lv_obj_set_size(kb, LV_PCT(100), 400);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_28, 0);   // ASCII keys + LV_SYMBOL_* (backspace, OK, …)
    lv_obj_set_style_bg_color(kb, lv_color_white(), 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, kInk, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, kInk, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, kInk, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_white(), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        DoSearch(st, lv_keyboard_get_textarea(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    }, LV_EVENT_READY, s);
    lv_obj_t* btn = ui::Button(body, "Tìm", [s, ta] { DoSearch(s, ta); }, LV_PCT(100), 70);
    ui::Invert(btn, true);
}

lv_obj_t* Tool(lv_obj_t* parent, const char* symbol, const char* text, ui::Action a, bool on = false) {
    lv_obj_t* b = ui::Button(parent, "", std::move(a), 104, 104);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_align(ui::IconLabel(b, symbol), LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* l = ui::Label(b, text, ui::F().small);
    lv_obj_set_style_text_letter_space(l, -1, 0);   // six tools on a 680 px row: "Dấu trang" must fit
    lv_obj_add_flag(b, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_width(l, 128);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -4);
    ui::Invert(b, on);
    return b;
}

void OpenMenu(State* s) {
    if (s->overlay) { CloseOverlay(s); return; }
    lv_obj_t* ov = NewOverlay(s, true);
    lv_obj_t* top = Panel(ov, 0, 118, false);
    lv_obj_align(ui::Button(top, std::string(icon::CHEV_L) + " Kệ sách", [] { OpenLibrary(); }, 170, 70), LV_ALIGN_LEFT_MID, 0, 0);
    const std::string bp = s->book.Path(), bt = s->book.Title();
    lv_obj_t* share = ui::Button(top, std::string(icon::SHARE), [bp, bt] { OpenShareBooks(bt, {bp}); }, 76, 70);
    lv_obj_align(share, LV_ALIGN_RIGHT_MID, 0, 0);
    const int tw = ui::ScreenW() - 48 - 190 - 90;
    lv_obj_t* t = ui::Label(top, s->book.Title(), ui::F().serif_book);
    lv_obj_set_size(t, tw, lv_font_get_line_height(ui::F().serif_book));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 190, 0);
    lv_obj_t* a = ui::Label(top, s->book.Author().empty() ? "Không rõ tác giả" : s->book.Author(), ui::F().serif_italic);
    lv_obj_set_style_text_color(a, kMuted, 0);
    lv_obj_set_size(a, tw, lv_font_get_line_height(ui::F().serif_italic));
    lv_label_set_long_mode(a, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(a, LV_ALIGN_BOTTOM_LEFT, 190, 0);

    lv_obj_t* bot = Panel(ov, ui::ScreenH() - 270, 270, true);
    char where[96];
    std::snprintf(where, sizeof where, "Chương %d / %d  ·  Trang %d / %d  ·  %d%%", s->chapter + 1, s->book.ChapterCount(),
                  s->page + 1, (int)s->pages.size(), BookPercent(s));
    lv_obj_t* w = ui::Label(bot, where, ui::F().small);
    lv_obj_set_style_text_color(w, kMuted, 0);
    lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(ui::Button(bot, std::string(icon::CHEV_L) + " Chương trước", [s] { if (s->chapter > 0) JumpTo(s, s->chapter - 1, 0, 0); }, 250, 60),
                 LV_ALIGN_TOP_LEFT, 0, 38);
    lv_obj_align(ui::Button(bot, std::string("Chương sau ") + icon::CHEV_R, [s] { if (s->chapter + 1 < s->book.ChapterCount()) JumpTo(s, s->chapter + 1, 0, 0); }, 250, 60),
                 LV_ALIGN_TOP_RIGHT, 0, 38);
    lv_obj_t* tools = Plain(bot);
    lv_obj_set_size(tools, LV_PCT(100), 108);
    lv_obj_align(tools, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tools, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Tool(tools, icon::LIST, "Mục lục", [s] { s->toc_page = -1; OpenToc(s); });
    Tool(tools, icon::BOOKMARK, "Dấu trang", [s] { OpenBookmarks(s); });
    Tool(tools, icon::SEARCH, "Tìm", [s] { OpenSearch(s); });
    Tool(tools, icon::FONT, "Hiển thị", [s] { OpenDisplaySheet(s, 0); });
    Tool(tools, icon::NOTES, "Ghi chú", [] { OpenNotesForBook(ui::ReaderPath()); });
    Tool(tools, s->auto_turn ? icon::PAUSE : icon::PLAY, s->auto_turn ? "Dừng lật" : "Tự lật", [s] {
        if (s->auto_turn) { StopAutoTurn(s); CloseOverlay(s); ShowPage(s); }
        else { CloseOverlay(s); StartAutoTurn(s); ui::Toast("Tự lật mỗi " + std::to_string(s->set.auto_turn) + " giây. Chạm hoặc bấm phím để dừng.", 2000); }
    }, s->auto_turn != nullptr);
}


// ---------- selection, dictionary, notes ----------

void Ordered(State* s, int& pa, size_t& a, int& pb, size_t& b) {
    bool ab = s->sel_pa < s->sel_pb || (s->sel_pa == s->sel_pb && s->sel_a0 <= s->sel_b0);
    pa = ab ? s->sel_pa : s->sel_pb;
    a = ab ? s->sel_a0 : s->sel_b0;
    pb = ab ? s->sel_pb : s->sel_pa;
    b = ab ? s->sel_b1 : s->sel_a1;
}

std::string SelectedText(State* s) {
    int pa, pb;
    size_t a, b;
    Ordered(s, pa, a, pb, b);
    std::string out;
    for (int p = pa; p <= pb && p < (int)s->paras.size(); ++p) {
        const std::string& t = s->paras[p].text;
        size_t from = p == pa ? a : 0, to = p == pb ? std::min(b, t.size()) : t.size();
        if (from < to) { if (!out.empty()) out += "\n"; out += t.substr(from, to - from); }
    }
    return out;
}

// The sentence around the selection: the example saved with a vocabulary word.
std::string Sentence(State* s) {
    if (s->sel_pa >= (int)s->paras.size()) return "";
    const std::string& t = s->paras[s->sel_pa].text;
    size_t a = s->sel_a0, b = s->sel_a1;
    while (a > 0 && t[a - 1] != '.' && t[a - 1] != '!' && t[a - 1] != '?') --a;
    while (b < t.size() && t[b] != '.' && t[b] != '!' && t[b] != '?') ++b;
    if (b < t.size()) ++b;
    std::string out = t.substr(a, b - a);
    while (!out.empty() && out[0] == ' ') out.erase(0, 1);
    return out.size() > 220 ? "" : out;
}

void ClearSelection(State* s) {
    s->selecting = false;
    if (s->selbar) { lv_obj_delete(s->selbar); s->selbar = nullptr; }
    ui::SetBackHandler([] { OpenLibrary(); });
    lv_obj_invalidate(s->page_obj);
}

void SaveNote(State* s, const std::string& text) {
    notes::Note n;
    n.book = s->book.Path();
    n.book_title = s->book.Title();
    n.chapter = s->chapter;
    n.chapter_title = s->book.ChapterTitle(s->chapter);
    Ordered(s, n.para, n.offset, n.para_end, n.offset_end);
    n.quote = SelectedText(s);
    n.text = text;
    notes::Add(n);
    s->notes = notes::ForBook(s->book.Path());
    ClearSelection(s);
    platform::Vibrate(settings::Get().haptic);
    ui::Toast(text.empty() ? "Đã lưu trích dẫn vào Ghi chú." : "Đã lưu ghi chú.", 1500);
}

void Lookup(State* s) {
    const std::string word = SelectedText(s), example = Sentence(s);
    lv_obj_t* ov = NewOverlay(s, true);
    const int h = std::min(ui::ScreenH() * 55 / 100, 640);
    lv_obj_t* panel = Panel(ov, ui::ScreenH() - h, h, true);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t* box = Plain(panel);
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 10, 0);
    FillLookup(box, word, example, true);
    lv_obj_t* row = Plain(panel);
    lv_obj_set_size(row, LV_PCT(100), 72);
    lv_obj_align(ui::Button(row, std::string(icon::DICT) + "  Mở từ điển", [word] { OpenDictionary(word); }, 280, 64), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(ui::Button(row, "Đóng", [s] { CloseOverlay(s); }, 160, 64), LV_ALIGN_RIGHT_MID, 0, 0);
    ClearSelection(s);
    auto alive = s->alive;
    ui::SetBackHandler([s, alive] { if (*alive) CloseOverlay(s); });
}

void ShowSelectionBar(State* s) {
    if (s->selbar) { lv_obj_delete(s->selbar); s->selbar = nullptr; }
    int pa, pb;
    size_t a, b;
    Ordered(s, pa, a, pb, b);
    // Put the bar away from the selection: at the top when the words are in the lower half.
    int sel_y = 0;
    for (const auto& ln : s->pages[s->page].lines) if (ln.para == pa && ln.from <= a) sel_y = ln.y;
    const bool top = sel_y > s->layout.height / 2;
    const int h = 200;
    lv_obj_t* bar = Panel(s->screen, top ? 0 : ui::ScreenH() - h, h, !top);
    s->selbar = bar;
    std::string text = SelectedText(s);
    for (auto& c : text) if (c == '\n') c = ' ';
    lv_obj_t* pv = ui::Label(bar, "“" + text + "”", ui::F().serif_italic);
    lv_obj_set_size(pv, ui::ScreenW() - 48, lv_font_get_line_height(ui::F().serif_italic));
    lv_label_set_long_mode(pv, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t* hint = ui::Label(bar, "Chạm một từ khác để chọn đến đó", ui::F().small);
    lv_obj_set_style_text_color(hint, kMuted, 0);
    lv_obj_set_pos(hint, 0, 36);
    lv_obj_t* tools = Plain(bar);
    lv_obj_set_size(tools, LV_PCT(100), 104);
    lv_obj_align(tools, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tools, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Tool(tools, icon::DICT, "Tra từ", [s] { Lookup(s); });
    Tool(tools, icon::HIGHLIGHT, "Trích dẫn", [s] { SaveNote(s, ""); });
    Tool(tools, icon::PEN, "Ghi chú", [s] {
        auto alive = s->alive;
        ui::PromptOptions o;
        o.title = "Ghi chú";
        o.hint = "“" + SelectedText(s).substr(0, 160) + "”";
        o.multiline = true;
        o.ok = "Lưu";
        ui::TextPrompt(o, [s, alive](const std::string& t) { if (*alive && s->selecting) SaveNote(s, t); });
    });
    Tool(tools, icon::PLUS, "Từ vựng", [s] {
        std::string w = dict::CleanWord(SelectedText(s));
        auto e = dict::LookupAll(w);
        std::string def = e.empty() ? "" : e[0].text.substr(0, e[0].text.find('\n')), phon;
        if (def.size() > 2 && def[0] == '/') { size_t q = def.find('/', 1); if (q != std::string::npos) { phon = def.substr(0, q + 1); def = def.substr(q + 1); } }
        while (!def.empty() && def[0] == ' ') def.erase(0, 1);
        bool ok = vocab::AddWord(e.empty() ? w : e[0].word, phon, def.empty() ? "(chưa có nghĩa: tra thêm)" : def, Sentence(s));
        ClearSelection(s);
        ui::Toast(ok ? "Đã thêm \"" + w + "\" vào Từ của tôi." : "\"" + w + "\" đã có trong Từ của tôi.", 1500);
    });
    if (settings::Get().ai_enabled) Tool(tools, icon::SPARKLES, "Hỏi AI", [s] {
        std::string sel = SelectedText(s);
        ClearSelection(s);
        OpenAssistant(sel);
    });
    Tool(tools, icon::X, "Huỷ", [s] { ClearSelection(s); });
    auto alive = s->alive;
    ui::SetBackHandler([s, alive] { if (*alive) ClearSelection(s); });
}

// Page coordinates of the finger, false outside the text block.
bool PagePoint(State* s, lv_point_t p, int& x, int& y) {
    lv_area_t a;
    lv_obj_get_coords(s->page_obj, &a);
    x = p.x - a.x1;
    y = p.y - a.y1;
    return x >= -20 && y >= -10 && x < s->layout.width + 20 && y < s->layout.height + 10;
}

void LongPress(State* s, lv_point_t p) {
    int x, y, para;
    size_t from, to;
    s->swallow_click = true;   // whatever happens, the release must not turn the page
    if (s->overlay || s->pages.empty() || !PagePoint(s, p, x, y)) return;
    if (!reader::HitWord(s->pages[s->page], s->paras, s->layout, x, y, para, from, to)) return;
    s->selecting = true;
    s->sel_pa = s->sel_pb = para;
    s->sel_a0 = s->sel_b0 = from;
    s->sel_a1 = s->sel_b1 = to;
    platform::Vibrate(settings::Get().haptic);
    lv_obj_invalidate(s->page_obj);
    ShowSelectionBar(s);
}

// While selecting, a tap on another word moves the end of the selection; elsewhere cancels.
void ExtendTo(State* s, lv_point_t p) {
    int x, y, para;
    size_t from, to;
    if (!PagePoint(s, p, x, y) || !reader::HitWord(s->pages[s->page], s->paras, s->layout, x, y, para, from, to)) {
        ClearSelection(s);
        return;
    }
    s->sel_pb = para;
    s->sel_b0 = from;
    s->sel_b1 = to;
    lv_obj_invalidate(s->page_obj);
    ShowSelectionBar(s);
}

}  // namespace

void OpenReaderAt(const std::string& path, int chapter, int para, size_t offset) {
    reader::Progress p;
    reader::GetProgress(path, p);
    p.chapter = chapter;
    p.para = para;
    p.offset = offset;
    reader::SaveProgress(path, p);
    OpenReader(path);
}

void OpenReader(const std::string& path) {
    ui::PrepareOrientation(true);   // landscape reading, if chosen in quick settings
    auto* s = new State();
    std::string err;
    if (!s->book.Open(path, err)) {
        delete s;
        ui::Toast("Không mở được sách: " + err);
        return;
    }
    s->set = reader::GetSettings();
    s->marks = reader::GetBookmarks(path);
    s->notes = notes::ForBook(path);
    s->screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s->screen, lv_color_white(), 0);
    lv_obj_remove_flag(s->screen, LV_OBJ_FLAG_SCROLLABLE);
    s->alive = ui::Alive(s->screen);
    lv_obj_add_event_cb(s->screen, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        if (st->page_shown_ms) reader::AddReading(0, (int)std::min<uint32_t>((lv_tick_get() - st->page_shown_ms) / 1000, 300));
        StopAutoTurn(st);
        if (st->clock) lv_timer_delete(st->clock);
        delete st;
    }, LV_EVENT_DELETE, s);

    // The page: drawn line by line by reader::DrawPage (no label per paragraph).
    s->page_obj = Plain(s->screen);
    ui::MarkGray(s->page_obj, ui::GrayRole::ReaderText);   // anti-aliased book text, like the stock reader
    lv_obj_add_event_cb(s->page_obj, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        if (st->pages.empty()) return;
        lv_area_t a;
        lv_obj_get_coords(st->page_obj, &a);
        std::vector<reader::Mark> marks;
        for (const auto& n : st->notes)
            if (n.chapter == st->chapter && !n.quote.empty()) marks.push_back({n.para, n.offset, n.para_end, n.offset_end, false});
        if (st->selecting) {
            bool ab = st->sel_pa < st->sel_pb || (st->sel_pa == st->sel_pb && st->sel_a0 <= st->sel_b0);
            reader::Mark m;
            m.selection = true;
            m.para_from = ab ? st->sel_pa : st->sel_pb;
            m.from = ab ? st->sel_a0 : st->sel_b0;
            m.para_to = ab ? st->sel_pb : st->sel_pa;
            m.to = ab ? st->sel_b1 : st->sel_a1;
            marks.push_back(m);
        }
        reader::DrawPage(lv_event_get_layer(e), a, st->pages[st->page], st->paras, st->layout, kInk, marks);
    }, LV_EVENT_DRAW_MAIN, s);

    const int W = ui::ScreenW(), H = ui::ScreenH();
    auto muted = [](lv_obj_t* l) { lv_obj_set_style_text_color(l, kMuted, 0); return l; };
    // Header: chapter · clock (centre) · battery ring (corner), one centre line at y = 30.
    const int hl = lv_font_get_line_height(ui::F().small);
    s->head_left = muted(ui::Label(s->screen, "", ui::F().small));
    lv_obj_set_size(s->head_left, W / 2 - 80, hl);
    lv_label_set_long_mode(s->head_left, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s->head_left, 30, 30 - hl / 2);
    s->head_right = muted(ui::Label(s->screen, "", ui::F().small));
    lv_obj_set_width(s->head_right, 120);
    lv_obj_set_style_text_align(s->head_right, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s->head_right, W / 2 - 60, 30 - hl / 2);
    s->battery = ui::BatteryIcon(s->screen, 22, true);   // small; % only if chosen in Cài đặt › Pin
    lv_obj_align(s->battery, LV_ALIGN_TOP_RIGHT, -30, 30 - 11);
    s->foot_track = Plain(s->screen);
    lv_obj_set_size(s->foot_track, W - 60, 2);
    lv_obj_set_pos(s->foot_track, 30, H - kFooterH + 6);
    lv_obj_set_style_bg_opa(s->foot_track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s->foot_track, lv_color_hex(0xd6d3cb), 0);
    s->foot_fill = Plain(s->screen);
    lv_obj_set_size(s->foot_fill, 2, 2);
    lv_obj_set_pos(s->foot_fill, 30, H - kFooterH + 6);
    lv_obj_set_style_bg_opa(s->foot_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s->foot_fill, kInk, 0);
    s->foot_left = muted(ui::Label(s->screen, "", ui::F().small));
    lv_obj_set_pos(s->foot_left, 30, H - kFooterH + 18);
    s->foot_mid = muted(ui::Label(s->screen, "", ui::F().small));
    lv_obj_set_width(s->foot_mid, W - 300);
    lv_label_set_long_mode(s->foot_mid, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s->foot_mid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s->foot_mid, 150, H - kFooterH + 18);
    s->foot_right = muted(ui::Label(s->screen, "", ui::F().small));
    lv_obj_set_pos(s->foot_right, W - 30 - 70, H - kFooterH + 18);
    lv_obj_set_width(s->foot_right, 70);
    lv_obj_set_style_text_align(s->foot_right, LV_TEXT_ALIGN_RIGHT, 0);

    // Bookmark ribbon in the top-right corner (Kindle style).
    s->ribbon = Plain(s->screen);
    lv_obj_set_size(s->ribbon, 30, 58);
    lv_obj_set_pos(s->ribbon, W - 110, 0);   // left of the battery ring
    lv_obj_set_style_bg_opa(s->ribbon, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s->ribbon, kInk, 0);
    lv_obj_t* notch = Plain(s->ribbon);
    lv_obj_set_size(notch, 22, 22);
    lv_obj_set_style_bg_opa(notch, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(notch, lv_color_white(), 0);
    lv_obj_set_style_transform_rotation(notch, 450, 0);
    lv_obj_set_style_transform_pivot_x(notch, 11, 0);
    lv_obj_set_style_transform_pivot_y(notch, 11, 0);
    lv_obj_align(notch, LV_ALIGN_BOTTOM_MID, 0, 15);

    // Tap zones over the whole page.
    lv_obj_t* tap = Plain(s->screen);
    lv_obj_set_size(tap, W, H);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        if (st->swallow_click) { st->swallow_click = false; return; }
        if (st->auto_turn) { StopAutoTurn(st); ShowPage(st); ui::Toast("Đã dừng tự lật trang", 1200); return; }
        lv_indev_t* in = lv_indev_active();
        if (st->selecting && in && lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER) {
            lv_point_t sp;
            lv_indev_get_point(in, &sp);
            ExtendTo(st, sp);
            return;
        }
        if (in && lv_indev_get_type(in) == LV_INDEV_TYPE_KEYPAD) { OpenMenu(st); return; }   // OK key
        lv_point_t p;
        lv_indev_get_point(in, &p);
        const int w = ui::ScreenW(), h = ui::ScreenH();
        if (p.x > w - 110 && p.y < 110) { ToggleBookmark(st); return; }   // top-right corner
        if (p.x > w / 3 && p.x < w * 2 / 3 && p.y > h / 4 && p.y < h * 3 / 4) { OpenMenu(st); return; }
        const bool left = p.x < w / 3;
        Turn(st, (left != st->set.left_hand) ? -1 : +1);
    }, LV_EVENT_CLICKED, s);
    lv_obj_add_event_cb(tap, [](lv_event_t* e) {   // long press: select a word (dictionary, quote, note)
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        lv_indev_t* in = lv_indev_active();
        if (!in || lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER) return;
        lv_point_t p;
        lv_indev_get_point(in, &p);
        LongPress(st, p);
    }, LV_EVENT_LONG_PRESSED, s);

    ComputeLayout(s);
    reader::Progress p;
    bool resume = reader::GetProgress(path, p);
    int ch = resume ? std::clamp(p.chapter, 0, s->book.ChapterCount() - 1) : 0;
    LoadChapter(s, ch, resume ? p.para : 0, resume ? p.offset : 0);
    while (!resume && s->paras.empty() && s->chapter + 1 < s->book.ChapterCount()) LoadChapter(s, s->chapter + 1, 0, 0);
    ShowPage(s);
    s->page_shown_ms = lv_tick_get();
    s->clock = lv_timer_create([](lv_timer_t* t) {   // keep the header clock current
        auto* st = static_cast<State*>(lv_timer_get_user_data(t));
        if (!st->set.status) return;
        lv_label_set_text(st->head_right, Clock().c_str());
        ui::SetBattery(st->battery, platform::BatteryPercent(), platform::Charging());
        lv_obj_align(st->battery, LV_ALIGN_TOP_RIGHT, -30, 30 - 11);
    }, 30000, s);
    ui::SetBackHandler([] { OpenLibrary(); });
    ui::SetPageKeyHandler([s](int dir) {
        if (s->auto_turn) { StopAutoTurn(s); ShowPage(s); }
        Turn(s, dir);
    });
    ui::SetAiMenuHandler([s] { OpenMenu(s); });
    auto ctx_alive = s->alive;
    ui::SetReadingContextProvider([s, ctx_alive] {
        ui::ReadingContext c;
        if (!*ctx_alive) return c;
        c.book_path = s->book.Path();
        c.title = s->book.Title();
        c.chapter = s->book.ChapterTitle(s->chapter);
        if (!s->pages.empty()) c.page_text = reader::PageText(s->pages[s->page], s->paras, 3000);
        return c;
    });
    ui::SetReaderOpen(path);
    ui::Show(s->screen);
}

}  // namespace screens
