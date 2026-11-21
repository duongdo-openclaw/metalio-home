// "Thư viện" (big shelf): every book on the SD card as a 3 x 3 grid of covers, nine per page.
//   Tabs:     Sách (all books) · Tác giả (authors → their books) · Thư mục (folders → books)
//   Tools:    Tìm (title/author, accent-insensitive, Telex keyboard) · Lọc (status, format, sort)
//             · Tải sách (online libraries from Google Drive)
// Page keys or ‹ › turn pages: no scrolling on e-ink.
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

#include "icons.h"
#include "library.h"
#include "platform.h"
#include "reading_state.h"
#include "screens.h"
#include "text_search.h"
#include "ui_common.h"

namespace screens {
namespace {

enum class Tab { Books, Authors, Folders };
enum class Status { All, Unread, Reading, Done };
enum class Sort { Recent, Title, Author, Added };
enum class Format { All, Epub, Txt };
constexpr int kCols = 3, kRowsN = 3, kPer = kCols * kRowsN;
constexpr int kListPer = 8;
constexpr int kCoverW = 126, kCoverH = 188, kStride = kCoverH + 76;
const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);

struct View {
    Tab tab = Tab::Books;
    Status status = Status::All;
    Sort sort = Sort::Recent;
    Format format = Format::All;
    std::string query;
    std::string group;          // selected author / folder ("" with in_group = root folder)
    bool in_group = false;
    int page = 0;
};
View g_view;                    // remembered between visits

struct State {
    View v;
    lv_obj_t* body = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* sheet = nullptr;  // filter sheet (layer_top)
    std::shared_ptr<bool> alive;
};

void Render(State* s);

lv_obj_t* Flat(lv_obj_t* parent, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

lv_obj_t* Pill(lv_obj_t* parent, const std::string& text, bool on, ui::Action a, int h = 60) {
    lv_obj_t* b = ui::Button(parent, "", std::move(a), LV_SIZE_CONTENT, h);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_border_width(b, on ? 2 : 1, 0);
    lv_obj_set_style_border_color(b, on ? lv_color_black() : kHair, 0);
    lv_obj_center(ui::Label(b, text, on ? ui::F().body_bold : ui::F().small));
    ui::Invert(b, on);
    return b;
}

std::string Fold(const std::string& s) { return search::Fold(s); }
long long Recent(const std::string& p) { reader::Progress pr; return reader::GetProgress(p, pr) ? pr.updated : 0LL; }
std::string AuthorOf(const std::string& p) {
    const std::string& a = library::LoadMeta(p).author;
    return a.empty() ? "Không rõ tác giả" : a;
}

bool Matches(const View& v, const std::string& p) {
    reader::Progress pr;
    bool started = reader::GetProgress(p, pr);
    switch (v.status) {
    case Status::Unread: if (started) return false; break;
    case Status::Reading: if (!started || pr.percent >= 99) return false; break;
    case Status::Done: if (!started || pr.percent < 99) return false; break;
    default: break;
    }
    std::string lower = Fold(p);
    if (v.format == Format::Epub && lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".epub") != 0) return false;
    if (v.format == Format::Txt && lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".txt") != 0) return false;
    if (!v.query.empty()) {
        const auto& m = library::LoadMeta(p);
        std::string hay = Fold(m.title + " " + m.author + " " + p.substr(p.rfind('/') + 1));
        if (hay.find(Fold(v.query)) == std::string::npos) return false;
    }
    if (v.in_group && v.tab == Tab::Authors && AuthorOf(p) != v.group) return false;
    if (v.in_group && v.tab == Tab::Folders && library::Folder(p) != v.group) return false;
    return true;
}

std::vector<std::string> Books(const View& v) {
    std::vector<std::string> out;
    for (const auto& p : library::Books()) if (Matches(v, p)) out.push_back(p);
    switch (v.sort) {
    case Sort::Recent: std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return Recent(a) > Recent(b); }); break;
    case Sort::Title: std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return Fold(library::LoadMeta(a).title) < Fold(library::LoadMeta(b).title); }); break;
    case Sort::Author: std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return Fold(AuthorOf(a)) < Fold(AuthorOf(b)); }); break;
    case Sort::Added: std::reverse(out.begin(), out.end()); break;   // card order is by path; newest copies last
    }
    return out;
}

// Groups (authors or folders) with the books matching the current filters.
std::vector<std::pair<std::string, int>> Groups(const View& v) {
    View f = v;
    f.in_group = false;
    std::map<std::string, int> count;
    for (const auto& p : library::Books())
        if (Matches(f, p)) count[v.tab == Tab::Authors ? AuthorOf(p) : library::Folder(p)]++;
    std::vector<std::pair<std::string, int>> out(count.begin(), count.end());
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return Fold(a.first) < Fold(b.first); });
    return out;
}

bool g_long_pressed = false;   // a long press opens "Chia sẻ"; the release must not open the book

void Cell(lv_obj_t* grid, const std::string& path, int col, int row) {
    const library::Meta& m = library::LoadMeta(path);
    const int cw = (ui::kW - 2 * ui::kPad) / kCols;
    lv_obj_t* hit = ui::Button(grid, "", [path] {
        if (g_long_pressed) { g_long_pressed = false; return; }
        OpenReader(path);
    }, cw, kCoverH + 72);
    auto* pp = new std::string(path);
    lv_obj_add_event_cb(hit, [](lv_event_t* e) {   // giữ bìa sách: chia sẻ cuốn này
        g_long_pressed = true;
        auto* p = static_cast<std::string*>(lv_event_get_user_data(e));
        OpenShareBooks(library::LoadMeta(*p).title, {*p});
    }, LV_EVENT_LONG_PRESSED, pp);
    lv_obj_add_event_cb(hit, [](lv_event_t* e) { delete static_cast<std::string*>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, pp);
    lv_obj_set_pos(hit, col * cw, row * kStride);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_set_style_pad_all(hit, 0, 0);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, LV_STATE_PRESSED);
    const int x = (cw - kCoverW) / 2;
    lv_obj_t* sh = Flat(hit, kCoverW, kCoverH);   // soft shadow
    lv_obj_set_pos(sh, x + 4, 5);
    lv_obj_set_style_bg_opa(sh, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sh, lv_color_hex(0xcfccc4), 0);
    lv_obj_set_style_radius(sh, 4, 0);
    lv_obj_t* c = ui::Cover(hit, m.cover, m.title, m.author, kCoverW, kCoverH);
    lv_obj_set_pos(c, x, 0);
    reader::Progress pr;
    bool started = reader::GetProgress(path, pr);
    if (started && pr.percent >= 99) {             // "read" ribbon in the corner
        lv_obj_t* badge = Flat(hit, 44, 44);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(badge, lv_color_black(), 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(badge, x + kCoverW - 50, 6);
        lv_obj_t* ck = ui::IconLabel(badge, icon::CHECK, ui::IconFont(28));
        lv_obj_set_style_text_color(ck, lv_color_white(), 0);
        lv_obj_center(ck);
    }
    if (started && pr.percent < 99) {
        lv_obj_t* track = Flat(hit, kCoverW, 3);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(track, lv_color_hex(0xdedbd3), 0);
        lv_obj_set_pos(track, x, kCoverH + 7);
        lv_obj_t* fill = Flat(hit, std::max(3, kCoverW * pr.percent / 100), 3);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(fill, lv_color_black(), 0);
        lv_obj_set_pos(fill, x, kCoverH + 7);
    }
    lv_obj_t* t = ui::Label(hit, m.title, ui::F().reader(22, true));
    lv_obj_set_size(t, kCoverW + 20, lv_font_get_line_height(ui::F().reader(22, true)));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(t, x - 10 < 0 ? 0 : x - 10, kCoverH + 14);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    std::string sub = !started ? AuthorOf(path) : pr.percent >= 99 ? "Đã đọc xong" : std::to_string(pr.percent) + "% · " + AuthorOf(path);
    lv_obj_t* a = ui::Label(hit, sub, ui::F().small);
    lv_obj_set_style_text_color(a, kMuted, 0);
    lv_obj_set_size(a, kCoverW + 20, lv_font_get_line_height(ui::F().small));
    lv_label_set_long_mode(a, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(a, x - 10 < 0 ? 0 : x - 10, kCoverH + 42);
}

void GroupRow(lv_obj_t* list, State* s, const std::string& name, int count) {
    const bool folder = s->v.tab == Tab::Folders;
    lv_obj_t* row = ui::Button(list, "", [s, name] { s->v.group = name; s->v.in_group = true; s->v.page = 0; Render(s); },
                               LV_PCT(100), 96);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, kHair, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_align(ui::IconLabel(row, folder ? icon::FOLDER : icon::USER), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* n = ui::Label(row, folder && name.empty() ? "Thư mục gốc của thẻ" : name, ui::F().body_bold);
    lv_obj_set_size(n, 430, lv_font_get_line_height(ui::F().body_bold));
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 62, 0);
    lv_obj_align(ui::Label(row, std::to_string(count) + " cuốn  " + icon::CHEV_R, ui::F().small), LV_ALIGN_RIGHT_MID, 0, 0);
}

void CloseSheet(State* s) {
    if (!s->sheet) return;
    lv_obj_delete(s->sheet);
    s->sheet = nullptr;
    ui::SetBackHandler([] { OpenLauncher(); });
}

void OpenFilter(State* s);

void SheetChips(lv_obj_t* parent, State* s, const char* title, const std::vector<std::string>& names, int cur,
                std::function<void(View&, int)> set) {
    lv_obj_t* h = ui::Label(parent, title, ui::F().small);
    lv_obj_set_style_text_letter_space(h, 3, 0);
    lv_obj_set_style_text_color(h, kMuted, 0);
    lv_obj_t* row = Flat(parent, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(row, 10, 0);
    for (int i = 0; i < (int)names.size(); ++i)
        Pill(row, names[i], i == cur, [s, set, i] { set(s->v, i); s->v.page = 0; Render(s); CloseSheet(s); OpenFilter(s); }, 64);
}

// Filter sheet slides over the bottom of the shelf (layer_top), changes apply immediately.
void OpenFilter(State* s) {
    CloseSheet(s);
    lv_obj_t* dim = lv_obj_create(lv_layer_top());
    s->sheet = dim;
    lv_obj_set_size(dim, ui::kW, ui::kH);
    lv_obj_set_style_bg_opa(dim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dim, 0, 0);
    lv_obj_add_flag(dim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dim, [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) CloseSheet(static_cast<State*>(lv_event_get_user_data(e)));
    }, LV_EVENT_CLICKED, s);
    lv_obj_t* p = lv_obj_create(dim);
    lv_obj_set_size(p, ui::kW, LV_SIZE_CONTENT);
    lv_obj_align(p, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_border_side(p, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(p, 4, 0);
    lv_obj_set_style_border_color(p, lv_color_black(), 0);
    lv_obj_set_style_pad_all(p, 24, 0);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* head = Flat(p, LV_PCT(100), 64);
    lv_obj_align(ui::Label(head, std::string(icon::SLIDERS) + "  Lọc & sắp xếp", ui::F().title), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* done = ui::Button(head, "Xong", [s] { CloseSheet(s); }, 130, 60);
    ui::Invert(done, true);
    lv_obj_align(done, LV_ALIGN_RIGHT_MID, 0, 0);
    SheetChips(p, s, "TRẠNG THÁI", {"Tất cả", "Chưa đọc", "Đang đọc", "Đã xong"}, (int)s->v.status,
               [](View& v, int i) { v.status = (Status)i; });
    SheetChips(p, s, "SẮP XẾP", {"Đọc gần đây", "Tên sách", "Tác giả", "Mới thêm"}, (int)s->v.sort,
               [](View& v, int i) { v.sort = (Sort)i; });
    SheetChips(p, s, "ĐỊNH DẠNG", {"Tất cả", "EPUB", "TXT"}, (int)s->v.format, [](View& v, int i) { v.format = (Format)i; });
    ui::Button(p, "Bỏ hết bộ lọc", [s] {
        s->v.status = Status::All; s->v.format = Format::All; s->v.sort = Sort::Recent; s->v.query.clear(); s->v.page = 0;
        Render(s); CloseSheet(s);
    }, LV_PCT(100), 70);
    ui::SetBackHandler([s] { CloseSheet(s); });
}

void Render(State* s) {
    g_view = s->v;
    lv_obj_clean(s->body);
    View& v = s->v;
    const int total = (int)library::Books().size();

    // Tabs: Sách · Tác giả · Thư mục
    lv_obj_t* tabs = Flat(s->body, LV_PCT(100), 66);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_border_width(tabs, 1, 0);
    lv_obj_set_style_border_side(tabs, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(tabs, kHair, 0);
    const std::pair<Tab, std::string> ts[] = {{Tab::Books, std::string(icon::BOOK) + " Sách"},
                                              {Tab::Authors, std::string(icon::USER) + " Tác giả"},
                                              {Tab::Folders, std::string(icon::FOLDER) + " Thư mục"}};
    for (auto& t : ts) {
        Tab id = t.first;
        const bool on = v.tab == id;
        lv_obj_t* b = ui::Button(tabs, "", [s, id] { s->v.tab = id; s->v.in_group = false; s->v.group.clear(); s->v.page = 0; Render(s); },
                                 (ui::kW - 2 * ui::kPad) / 3, 64);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, on ? 4 : 0, 0);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_center(ui::Label(b, t.second, on ? ui::F().body_bold : ui::F().body));
    }

    // Tools: search box · filter · download
    lv_obj_t* tools = Flat(s->body, LV_PCT(100), 66);
    lv_obj_t* sb = ui::Button(tools, "", [s] {
        ui::PromptOptions o;
        o.title = "Tìm sách";
        o.hint = "Tên sách hoặc tác giả. Gõ không dấu cũng tìm được.";
        o.initial = s->v.query;
        o.placeholder = "Ví dụ: nguyen nhat anh";
        o.ok = "Tìm";
        auto alive = s->alive;
        ui::TextPrompt(o, [s, alive](const std::string& q) { if (!*alive) return; s->v.query = q; s->v.page = 0; Render(s); });
    }, 330, 62);
    lv_obj_set_style_radius(sb, 31, 0);
    lv_obj_set_style_border_width(sb, v.query.empty() ? 1 : 3, 0);
    lv_obj_set_style_border_color(sb, v.query.empty() ? kHair : lv_color_black(), 0);
    lv_obj_t* sl = ui::Label(sb, std::string(icon::SEARCH) + "  " + (v.query.empty() ? "Tìm sách, tác giả" : v.query), ui::F().small);
    lv_obj_set_width(sl, 290);
    lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_align(sb, LV_ALIGN_LEFT_MID, 0, 0);
    const bool filtered = v.status != Status::All || v.format != Format::All || v.sort != Sort::Recent;
    lv_obj_t* fb = Pill(tools, std::string(icon::SLIDERS) + " Lọc", filtered, [s] { OpenFilter(s); }, 62);
    lv_obj_align(fb, LV_ALIGN_LEFT_MID, 342, 0);
    lv_obj_t* db = Pill(tools, std::string(icon::DOWNLOAD) + " Tải sách", false, [] { OpenOnlineLibraries(); }, 62);
    lv_obj_set_style_border_width(db, 2, 0);
    lv_obj_set_style_border_color(db, lv_color_black(), 0);
    lv_obj_align(db, LV_ALIGN_RIGHT_MID, 0, 0);

    const bool group_list = v.tab != Tab::Books && !v.in_group;
    std::vector<std::pair<std::string, int>> groups;
    std::vector<std::string> books;
    if (group_list) groups = Groups(v);
    else books = Books(v);
    const int items = group_list ? (int)groups.size() : (int)books.size();
    const int per = group_list ? kListPer : kPer;
    const int pages = std::max(1, (items + per - 1) / per);
    v.page = std::clamp(v.page, 0, pages - 1);

    // Breadcrumb inside an author / folder, or the active query
    lv_obj_t* crumb = Flat(s->body, LV_PCT(100), 40);
    std::string where;
    if (v.in_group) where = std::string(icon::CHEV_L) + "  " + (v.tab == Tab::Authors ? "Tác giả: " : "Thư mục: ") +
                            (v.group.empty() ? "gốc" : v.group);
    else if (group_list) where = v.tab == Tab::Authors ? std::to_string(items) + " tác giả" : std::to_string(items) + " thư mục";
    else where = std::to_string(items) + (items == total ? " cuốn trên thẻ nhớ" : " / " + std::to_string(total) + " cuốn khớp bộ lọc");
    lv_obj_t* cl = ui::Label(crumb, where, v.in_group ? ui::F().body_bold : ui::F().small);
    lv_obj_set_width(cl, ui::kW - 2 * ui::kPad - (v.query.empty() ? 160 : 320));
    lv_label_set_long_mode(cl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(cl, LV_ALIGN_LEFT_MID, 0, 0);
    if (v.in_group) {
        lv_obj_add_flag(crumb, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(crumb, [](lv_event_t* e) {
            auto* st = static_cast<State*>(lv_event_get_user_data(e));
            st->v.in_group = false; st->v.page = 0; Render(st);
        }, LV_EVENT_CLICKED, s);
    }
    if (!group_list && !books.empty()) {   // share what is on the shelf now (all, a filter, an author…)
        std::string what = v.in_group ? (v.group.empty() ? std::string("Thư mục gốc") : v.group)
                          : !v.query.empty() ? "Kết quả tìm \"" + v.query + "\"" : "Kệ sách";
        lv_obj_t* sh = ui::Button(crumb, "", [books, what] { OpenShareBooks(what, books); }, LV_SIZE_CONTENT, 46);
        lv_obj_set_style_pad_hor(sh, 16, 0);
        lv_obj_set_style_radius(sh, 23, 0);
        lv_obj_set_style_border_width(sh, 1, 0);
        lv_obj_center(ui::Label(sh, std::string(icon::SHARE) + "  Chia sẻ", ui::F().small));
        lv_obj_align(sh, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    if (!v.query.empty()) {
        lv_obj_t* clr = ui::Button(crumb, "", [s] { s->v.query.clear(); s->v.page = 0; Render(s); }, LV_SIZE_CONTENT, 46);
        lv_obj_set_style_pad_hor(clr, 16, 0);
        lv_obj_set_style_radius(clr, 23, 0);
        lv_obj_set_style_border_width(clr, 1, 0);
        lv_obj_center(ui::Label(clr, std::string(icon::X) + "  Bỏ tìm", ui::F().small));
        lv_obj_align(clr, LV_ALIGN_RIGHT_MID, -160, 0);
    }

    lv_obj_t* grid = Flat(s->body, LV_PCT(100), kRowsN * kStride);
    if (items == 0) {
        std::string msg = total == 0 ? "Thẻ nhớ chưa có sách. Chép EPUB/TXT vào thẻ (thư mục nào cũng được), bấm \"Tải sách\" để lấy từ thư viện online, hoặc gửi qua Chuyển file."
                                     : "Không có cuốn nào khớp. Thử bỏ bộ lọc hoặc đổi từ khoá.";
        lv_obj_t* e = ui::Label(grid, msg, ui::F().serif_italic);
        lv_obj_set_width(e, LV_PCT(100));
        lv_label_set_long_mode(e, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_pos(e, 0, 40);
    }
    if (group_list) {
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
        for (int i = v.page * per; i < std::min(items, (v.page + 1) * per); ++i) GroupRow(grid, s, groups[i].first, groups[i].second);
    } else {
        for (int i = v.page * per, k = 0; i < std::min(items, (v.page + 1) * per); ++i, ++k) Cell(grid, books[i], k % kCols, k / kCols);
    }

    // Pager
    lv_obj_t* pager = Flat(s->body, LV_PCT(100), 56);
    lv_obj_t* prev = ui::Button(pager, std::string(icon::CHEV_L) + " Trước", [s] { s->v.page--; Render(s); }, 170, 54);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* next = ui::Button(pager, std::string("Sau ") + icon::CHEV_R, [s] { s->v.page++; Render(s); }, 170, 54);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);
    if (v.page == 0) lv_obj_add_state(prev, LV_STATE_DISABLED);
    if (v.page >= pages - 1) lv_obj_add_state(next, LV_STATE_DISABLED);
    char pg[32];
    std::snprintf(pg, sizeof pg, "Trang %d / %d", v.page + 1, pages);
    lv_obj_center(ui::Label(pager, pg, ui::F().body));

    lv_label_set_text_fmt(s->status, "%d cuốn trên thẻ nhớ · %s", total,
                          v.tab == Tab::Books ? "tất cả sách" : v.tab == Tab::Authors ? "theo tác giả" : "theo thư mục");
}

}  // namespace

void OpenLibrary() {
    auto* s = new State();
    s->v = g_view;
    ui::Page p = ui::NewPage("KỆ SÁCH", [] { OpenLauncher(); });
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        if (st->sheet) lv_obj_delete(st->sheet);
        delete st;
    }, LV_EVENT_DELETE, s);
    s->body = p.body;
    s->status = p.header_status;
    s->alive = ui::Alive(p.screen);
    lv_obj_set_style_pad_row(p.body, 6, 0);
    lv_obj_set_style_pad_top(p.body, 8, 0);
    lv_obj_remove_flag(p.body, LV_OBJ_FLAG_SCROLLABLE);
    library::Invalidate();   // pick up books copied over USB since the last visit
    ui::MarkGray(p.screen, ui::GrayRole::Shelf);
    ui::SetPageKeyHandler([s](int dir) { s->v.page += dir; Render(s); });
    Render(s);
    ui::Show(p.screen);
}

}  // namespace screens
