// Home = the bookshelf, designed like a printed page (Kobo-inspired):
//   status line · masthead (date, greeting, quote of the day) · the book in progress ·
//   three reading figures · one shelf row of covers with titles · bottom navigation.
// Hairlines and soft greys instead of heavy black frames; the screen is tagged as a grey
// "shelf" so the firmware renders it in 16 levels (EpdContentRole::Shelf).
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>

#include "book.h"
#include "library.h"
#include "settings.h"
#include "owner.h"
#include "platform.h"
#include "reading_state.h"
#include "screens.h"
#include "icons.h"
#include "ui_common.h"

namespace screens {
namespace {

constexpr int kMargin = 30;                          // page margin, wider than the UI default
constexpr int kInner = ui::kW - 2 * kMargin;         // 620
constexpr int kShelfCols = 4;
constexpr int kGap = 22;
constexpr int kCoverW = (kInner - (kShelfCols - 1) * kGap) / kShelfCols;  // 138
constexpr int kCoverH = kCoverW * 3 / 2;                                   // 207

const lv_color_t kInk = lv_color_hex(0x1a1a18);
const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);
const lv_color_t kShadow = lv_color_hex(0xcfccc4);

using Meta = library::Meta;
using library::LoadMeta;

struct Quote { const char* text; const char* author; };
const Quote kQuotes[] = {
    {"Sách mở ra trước mắt tôi những chân trời mới.", "Maxim Gorky"},
    {"Ngọc không mài không sáng, người không học không biết đạo.", "Lễ Ký"},
    {"Học, học nữa, học mãi.", "V. I. Lenin"},
    {"Mỗi ngày vài trang, một năm đi rất xa.", "Metalio"},
};

lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

lv_obj_t* Hairline(lv_obj_t* parent, int x, int y, int w, int thick = 1, lv_color_t c = kHair) {
    lv_obj_t* r = Box(parent, x, y, w, thick);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, c, 0);
    return r;
}

lv_obj_t* Text(lv_obj_t* parent, const std::string& s, const lv_font_t* f, lv_color_t c = kInk) {
    lv_obj_t* l = ui::Label(parent, s, f);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

// Small-caps style label: upper-case, tracked out, muted.
lv_obj_t* Eyebrow(lv_obj_t* parent, const std::string& s) {
    lv_obj_t* l = Text(parent, s, ui::F().small, kMuted);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    return l;
}

// One line, ellipsis when too long.
void OneLine(lv_obj_t* l, int w) {
    lv_obj_set_size(l, w, lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
}

// Cover with a soft drop shadow, so it sits on the page like a real book.
lv_obj_t* BookCover(lv_obj_t* parent, const Meta& m, int x, int y, int w, int h) {
    lv_obj_t* sh = Box(parent, x + 5, y + 6, w, h);
    lv_obj_set_style_bg_opa(sh, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sh, kShadow, 0);
    lv_obj_set_style_radius(sh, 4, 0);
    lv_obj_t* c = ui::Cover(parent, m.cover, m.title, m.author, w, h);
    lv_obj_set_pos(c, x, y);
    return c;
}

// Thin progress rule: grey track, ink fill.
void ProgressRule(lv_obj_t* parent, int x, int y, int w, int pct, int thick = 3) {
    Hairline(parent, x, y, w, thick, lv_color_hex(0xdedbd3));
    if (pct > 0) Hairline(parent, x, y, std::max(thick, w * std::min(pct, 100) / 100), thick, kInk);
}

std::string Minutes(int seconds) {
    int m = (seconds + 30) / 60;
    if (m < 60) return std::to_string(m);
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", m / 60, m % 60);
    return b;
}

struct State {
    lv_obj_t* screen = nullptr;
    std::vector<std::string> books;
    int shelf_page = 0;
    lv_obj_t* shelf = nullptr;
    lv_obj_t* shelf_count = nullptr;
};

// Status bar: owner on the left, clock in the middle, Wi-Fi + battery ring in the corner, all on
// one centre line.
void StatusLine(lv_obj_t* scr) {
    constexpr int kBarH = 56;
    lv_obj_t* bar = Box(scr, kMargin, 4, kInner, kBarH);
    owner::Info o = owner::Get();
    lv_obj_t* left = Text(bar, o.empty() ? "Tủ sách Metalio" : "Tủ sách của " + owner::FirstName(o), ui::F().serif_italic, kMuted);
    OneLine(left, 220);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    std::time_t t = std::time(nullptr);
    char clock[16];
    std::strftime(clock, sizeof clock, "%H:%M", std::localtime(&t));
    lv_obj_t* c = Text(bar, clock, ui::F().body_bold, kInk);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t* bat = ui::BatteryIcon(bar, 34, false);
    lv_obj_align(bat, LV_ALIGN_RIGHT_MID, 0, 0);
    if (!platform::WifiName().empty()) {
        lv_obj_update_layout(bat);
        lv_obj_t* w = ui::IconLabel(bar, icon::WIFI, ui::IconFont(26));
        lv_obj_set_style_text_color(w, kMuted, 0);
        lv_obj_align(w, LV_ALIGN_RIGHT_MID, -lv_obj_get_width(bat) - 16, 0);
    }
}

int Masthead(lv_obj_t* scr) {
    owner::Info o = owner::Get();
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    static const char* wd[] = {"CHỦ NHẬT", "THỨ HAI", "THỨ BA", "THỨ TƯ", "THỨ NĂM", "THỨ SÁU", "THỨ BẢY"};
    char date[64];
    std::snprintf(date, sizeof date, "%s · %d THÁNG %d", wd[tm->tm_wday], tm->tm_mday, tm->tm_mon + 1);
    lv_obj_set_pos(Eyebrow(scr, date), kMargin, 70);

    int h = tm->tm_hour;
    std::string hello = h < 11 ? "Chào buổi sáng" : h < 13 ? "Chào buổi trưa" : h < 18 ? "Chào buổi chiều" : "Chào buổi tối";
    hello += o.empty() ? "." : ", " + owner::FirstName(o) + ".";
    lv_obj_t* g = Text(scr, hello, ui::F().reader(46, false));
    OneLine(g, kInner);
    lv_obj_set_pos(g, kMargin, 100);

    const Quote& q = kQuotes[tm->tm_yday % (sizeof kQuotes / sizeof kQuotes[0])];
    lv_obj_t* ql = Text(scr, std::string("“") + q.text + "”  — " + q.author, ui::F().serif_italic, kMuted);
    lv_obj_set_width(ql, kInner);
    lv_label_set_long_mode(ql, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(ql, 2, 0);
    lv_obj_set_pos(ql, kMargin, 170);
    lv_obj_update_layout(ql);
    int y = 170 + lv_obj_get_height(ql) + 22;
    Hairline(scr, kMargin, y, kInner);
    return y + 1;
}

int CurrentBook(lv_obj_t* scr, int y) {
    std::string path = reader::LastBook();
    const int ch = 282, cw = 188;
    lv_obj_t* card = ui::Button(scr, "", [path] { if (!path.empty()) OpenReader(path); }, ui::kW - 2 * kMargin + 20, ch + 40);
    lv_obj_set_pos(card, kMargin - 10, y + 10);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(card, 0, 0);
    if (path.empty()) {
        lv_obj_t* e = Text(card, "Chưa mở cuốn nào.", ui::F().reader(34, false));
        lv_obj_set_pos(e, 10, 40);
        lv_obj_t* s = Text(card, "Chọn một cuốn trong tủ sách bên dưới, hoặc tìm sách mới ở Khám phá.", ui::F().serif_italic, kMuted);
        lv_obj_set_width(s, kInner);
        lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_pos(s, 10, 100);
        return y + 200;
    }
    const Meta& m = LoadMeta(path);
    reader::Progress p;
    reader::GetProgress(path, p);
    BookCover(card, m, 10, 14, cw, ch);

    const int x = 10 + cw + 34, w = kInner - cw - 34;
    lv_obj_set_pos(Eyebrow(card, "ĐANG ĐỌC"), x, 16);
    lv_obj_t* t = Text(card, m.title, ui::F().reader(34, true));
    lv_obj_set_style_text_line_space(t, 2, 0);
    lv_obj_set_width(t, w);
    lv_obj_set_height(t, lv_font_get_line_height(ui::F().reader(34, true)) * 2 + 2);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(t, x, 50);
    lv_obj_t* a = Text(card, m.author.empty() ? "Không rõ tác giả" : m.author, ui::F().serif_italic, kMuted);
    OneLine(a, w);
    lv_obj_set_pos(a, x, 152);

    char chap[48];
    std::snprintf(chap, sizeof chap, "Chương %d / %d", p.chapter + 1, std::max(1, m.chapters));
    lv_obj_set_pos(Text(card, chap, ui::F().small, kMuted), x, 192);
    char pc[16];
    std::snprintf(pc, sizeof pc, "%d%%", p.percent);
    lv_obj_t* pl = Text(card, pc, ui::F().small, kInk);
    lv_obj_update_layout(pl);
    lv_obj_set_pos(pl, x + w - lv_obj_get_width(pl), 192);
    ProgressRule(card, x, 226, w, p.percent);

    lv_obj_t* go = ui::Button(card, "", [path] { OpenReader(path); }, 190, 56);
    lv_obj_set_style_radius(go, 28, 0);
    lv_obj_set_style_border_width(go, 2, 0);
    lv_obj_set_style_border_color(go, kInk, 0);
    lv_obj_t* gl = Text(go, "Đọc tiếp  ›", ui::F().body_bold);
    lv_obj_center(gl);
    lv_obj_set_pos(go, x, 244);
    return y + ch + 40;
}

// Reading goals (settings › Mục tiêu đọc): today against the daily goal, the streak, and books
// finished this year against the yearly goal. Tapping the row opens the goal page.
int Figures(lv_obj_t* scr, int y) {
    reader::Stats st = reader::GetStats();
    const auto& v = settings::Get();
    std::time_t now = std::time(nullptr);
    const int year = std::localtime(&now)->tm_year + 1900;
    const int done_books = library::FinishedInYear(year);
    const int today_min = st.today_seconds / 60;
    struct Fig { std::string num, label; int pct; };
    const Fig figs[] = {
        {v.goal_minutes ? std::to_string(today_min) + "/" + std::to_string(v.goal_minutes) : Minutes(st.today_seconds),
         v.goal_minutes && today_min >= v.goal_minutes ? "phút · đã đạt" : "phút hôm nay",
         v.goal_minutes ? std::min(100, today_min * 100 / v.goal_minutes) : -1},
        {std::to_string(st.streak_days), "ngày liên tục", -1},
        {v.goal_books ? std::to_string(done_books) + "/" + std::to_string(v.goal_books) : std::to_string(done_books),
         "cuốn năm " + std::to_string(year), v.goal_books ? std::min(100, done_books * 100 / v.goal_books) : -1},
    };
    lv_obj_t* hit = ui::Button(scr, "", [] { OpenGoals(); }, kInner, 104);
    lv_obj_set_pos(hit, kMargin, y);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_set_style_pad_all(hit, 0, 0);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, LV_STATE_PRESSED);
    const int colw = kInner / 3;
    for (int i = 0; i < 3; ++i) {
        int x = i * colw;
        if (i) Hairline(hit, x, 18, 1, 62);
        lv_obj_t* n = Text(hit, figs[i].num, ui::F().reader(42, false));
        lv_obj_update_layout(n);
        lv_obj_set_pos(n, x + (colw - lv_obj_get_width(n)) / 2, 0);
        lv_obj_t* l = Text(hit, figs[i].label, ui::F().small, kMuted);
        lv_obj_update_layout(l);
        lv_obj_set_pos(l, x + (colw - lv_obj_get_width(l)) / 2, 56);
        if (figs[i].pct >= 0) ProgressRule(hit, x + 34, 94, colw - 68, figs[i].pct, 3);
    }
    y += 108;
    Hairline(scr, kMargin, y, kInner);
    return y + 1;
}

void RenderShelf(State* s) {
    lv_obj_clean(s->shelf);
    const int n = (int)s->books.size();
    const int pages = std::max(1, (n + kShelfCols - 1) / kShelfCols);
    s->shelf_page = std::clamp(s->shelf_page, 0, pages - 1);
    char cnt[48];
    std::snprintf(cnt, sizeof cnt, "%d cuốn   %d / %d", n, s->shelf_page + 1, pages);
    lv_label_set_text(s->shelf_count, cnt);
    if (n == 0) {
        lv_obj_t* e = Text(s->shelf, "Tủ sách còn trống. Tải sách ở Khám phá, hoặc gửi từ điện thoại qua Ứng dụng › Chuyển file.",
                           ui::F().serif_italic, kMuted);
        lv_obj_set_width(e, kInner);
        lv_label_set_long_mode(e, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_pos(e, 0, 40);
        return;
    }
    for (int c = 0; c < kShelfCols; ++c) {
        int i = s->shelf_page * kShelfCols + c;
        if (i >= n) break;
        std::string path = s->books[i];
        const Meta& m = LoadMeta(path);
        int x = c * (kCoverW + kGap);
        lv_obj_t* hit = ui::Button(s->shelf, "", [path] { OpenReader(path); }, kCoverW + 8, kCoverH + 130);
        lv_obj_set_pos(hit, x, 0);
        lv_obj_set_style_border_width(hit, 0, 0);
        lv_obj_set_style_pad_all(hit, 0, 0);
        lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, LV_STATE_PRESSED);
        BookCover(hit, m, 0, 0, kCoverW, kCoverH);
        reader::Progress p;
        bool started = reader::GetProgress(path, p);
        ProgressRule(hit, 0, kCoverH + 16, kCoverW, started ? std::max(2, p.percent) : 0, 2);
        lv_obj_t* t = Text(hit, m.title, ui::F().reader(20, false));
        lv_obj_set_width(t, kCoverW);
        lv_obj_set_height(t, lv_font_get_line_height(ui::F().reader(20, false)) * 2);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(t, 0, kCoverH + 26);
        lv_obj_t* st = Text(hit, !started ? "Mới" : p.percent >= 99 ? "Đã đọc xong" : std::to_string(p.percent) + "%",
                            ui::F().small, kMuted);
        lv_obj_set_pos(st, 0, kCoverH + 26 + lv_font_get_line_height(ui::F().reader(20, false)) * 2 + 2);
    }
}

}  // namespace

namespace { lv_obj_t* g_home = nullptr; }
bool HomeActive() { return g_home && lv_screen_active() == g_home; }

void OpenLauncher() {
    ui::PrepareOrientation(false);
    auto* s = new State();
    s->screen = lv_obj_create(nullptr);
    g_home = s->screen;
    lv_obj_t* scr = s->screen;
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui::MarkGray(scr, ui::GrayRole::Shelf);   // soft greys and shadows need the 16-level mode
    lv_obj_add_event_cb(scr, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        if (g_home == st->screen) g_home = nullptr;
        delete st;
    }, LV_EVENT_DELETE, s);

    StatusLine(scr);
    lv_obj_t* pull = Box(scr, 0, 0, ui::kW, 56);   // tap the status line = pull down quick settings
    lv_obj_add_flag(pull, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pull, [](lv_event_t*) { OpenQuickSettings(); }, LV_EVENT_CLICKED, nullptr);
    int y = Masthead(scr);
    y = CurrentBook(scr, y);
    y = Figures(scr, y);

    // Shelf header
    lv_obj_t* eb = Eyebrow(scr, "TỦ SÁCH");
    lv_obj_set_pos(eb, kMargin, y + 18);
    lv_obj_add_flag(eb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(eb, 16);
    lv_obj_add_event_cb(eb, [](lv_event_t*) { OpenLibrary(); }, LV_EVENT_CLICKED, nullptr);
    s->shelf_count = Text(scr, "", ui::F().small, kMuted);
    lv_obj_set_pos(s->shelf_count, kMargin + 150, y + 18);
    lv_obj_t* all = ui::Button(scr, "", [] { OpenLibrary(); }, 130, 50);
    lv_obj_set_style_border_width(all, 0, 0);
    lv_obj_set_style_bg_opa(all, LV_OPA_TRANSP, 0);
    lv_obj_center(Text(all, std::string("Tất cả ") + icon::CHEV_R, ui::F().small, kInk));
    lv_obj_t* prev = ui::Button(scr, "‹", [s] { s->shelf_page--; RenderShelf(s); }, 60, 50);
    lv_obj_t* next = ui::Button(scr, "›", [s] { s->shelf_page++; RenderShelf(s); }, 60, 50);
    for (lv_obj_t* b : {prev, next}) {
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, kHair, 0);
        lv_obj_set_style_radius(b, 25, 0);
    }
    lv_obj_set_pos(all, ui::kW - kMargin - 128 - 146, y + 8);
    lv_obj_set_pos(prev, ui::kW - kMargin - 128, y + 8);
    lv_obj_set_pos(next, ui::kW - kMargin - 60, y + 8);

    s->shelf = Box(scr, kMargin, y + 76, kInner + 8, ui::kH - ui::kNavH - (y + 76));
    lv_obj_add_flag(s->shelf, LV_OBJ_FLAG_CLICKABLE);

    // Shelf order: most recently read first, then the rest by name. Books anywhere on the card.
    std::vector<std::pair<long long, std::string>> order;
    for (const auto& path : library::Books()) {
        reader::Progress p;
        long long when = reader::GetProgress(path, p) ? p.updated : 0;
        order.push_back({-when, path});
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& o : order) s->books.push_back(o.second);
    RenderShelf(s);

    ui::BottomNav(scr, ui::Tab::Home);
    ui::SetBackHandler(nullptr);
    ui::SetAiMenuHandler(nullptr);
    ui::SetPageKeyHandler([s](int dir) {
        int pages = std::max(1, ((int)s->books.size() + kShelfCols - 1) / kShelfCols);
        int n = s->shelf_page + dir;
        if (n >= 0 && n < pages) { s->shelf_page = n; RenderShelf(s); }
    });
    ui::Show(scr);
}


}  // namespace screens
