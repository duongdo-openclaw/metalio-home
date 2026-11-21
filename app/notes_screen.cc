// "Ghi chú": quotes saved while reading (with the reader's comment) and free notes.
//   List:   Tất cả · Theo sách · Ghi chú riêng, five per page, "+ Ghi chú mới".
//   Detail: the quote, the comment, "Mở trong sách" (jumps to the passage), Sửa, Xoá.
// Every book's notes are also exported to <sd>/notes/<book>.md (readable on a computer).
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>

#include "icons.h"
#include "notes.h"
#include "platform.h"
#include "screens.h"
#include "ui_common.h"

namespace screens {
namespace {

enum class Tab { All, Books, Free };
Tab g_tab = Tab::All;
std::string g_book;        // inside Tab::Books: selected book path ("" = book list)
int g_page = 0;
const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);

void List();

std::string Date(long long t) {
    std::time_t tt = (std::time_t)t;
    char b[32];
    std::strftime(b, sizeof b, "%d/%m/%Y", std::localtime(&tt));
    return b;
}

lv_obj_t* Plain(lv_obj_t* parent, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

void NoteRow(lv_obj_t* parent, const notes::Note& n) {
    int id = n.id;
    lv_obj_t* row = ui::Button(parent, "", [id] { OpenNote(id); }, LV_PCT(100), 170);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, kHair, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_ver(row, 12, 0);
    const bool quote = !n.quote.empty();
    if (quote) {   // vertical rule like a printed pull-quote
        lv_obj_t* bar = Plain(row, 4, 92);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
        lv_obj_set_pos(bar, 0, 4);
    }
    const int x = quote ? 20 : 0;
    lv_obj_t* q = ui::Label(row, quote ? n.quote : n.text, quote ? ui::F().serif_italic : ui::F().body);
    lv_obj_set_size(q, ui::kW - 2 * ui::kPad - x, lv_font_get_line_height(lv_obj_get_style_text_font(q, LV_PART_MAIN)) * 3 + 4);
    lv_label_set_long_mode(q, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(q, x, 0);
    std::string meta = (n.book.empty() ? std::string(icon::NOTE) + " Ghi chú riêng" : std::string(icon::BOOK) + " " + n.book_title) +
                       (quote && !n.text.empty() ? "  ·  " + std::string(icon::PEN) + " có ghi chú" : "") + "  ·  " + Date(n.updated);
    lv_obj_t* m = ui::Label(row, meta, ui::F().small);
    lv_obj_set_style_text_color(m, kMuted, 0);
    lv_obj_set_width(m, ui::kW - 2 * ui::kPad);
    lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(m, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void NewFree() {
    ui::PromptOptions o;
    o.title = "Ghi chú mới";
    o.hint = "Gõ Telex: aa=â, ow=ơ, dd=đ, s f r x j = sắc huyền hỏi ngã nặng.";
    o.multiline = true;
    o.ok = "Lưu";
    ui::TextPrompt(o, [](const std::string& t) {
        if (t.empty()) return;
        notes::Note n;
        n.text = t;
        OpenNote(notes::Add(n));
    });
}

void List() {
    ui::Page p = ui::NewPage("GHI CHÚ", [] { OpenApps(); });
    auto all = notes::All();
    lv_obj_remove_flag(p.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(p.body, 8, 0);
    int quotes = 0;
    for (auto& n : all) quotes += !n.quote.empty();
    lv_label_set_text_fmt(p.header_status, "%d ghi chú · %d trích dẫn từ sách", (int)all.size(), quotes);

    lv_obj_t* tabs = Plain(p.body, LV_PCT(100), 66);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    const std::pair<Tab, std::string> ts[] = {{Tab::All, "Tất cả"}, {Tab::Books, std::string(icon::BOOK) + " Theo sách"}, {Tab::Free, std::string(icon::NOTE) + " Riêng"}};
    for (auto& t : ts) {
        Tab id = t.first;
        lv_obj_t* b = ui::Button(tabs, "", [id] { g_tab = id; g_book.clear(); g_page = 0; List(); }, (ui::kW - 2 * ui::kPad) / 3, 64);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, g_tab == id ? 4 : 0, 0);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_center(ui::Label(b, t.second, g_tab == id ? ui::F().body_bold : ui::F().body));
    }

    std::vector<notes::Note> shown;
    std::vector<std::pair<std::string, std::pair<std::string, int>>> books;   // path → (title, count)
    const bool book_list = g_tab == Tab::Books && g_book.empty();
    if (book_list) {
        std::map<std::string, std::pair<std::string, int>> m;
        for (auto& n : all) if (!n.book.empty()) { m[n.book].first = n.book_title; m[n.book].second++; }
        for (auto& kv : m) books.push_back(kv);
    } else {
        for (auto& n : all) {
            if (g_tab == Tab::Free && !n.book.empty()) continue;
            if (g_tab == Tab::Books && n.book != g_book) continue;
            shown.push_back(n);
        }
        if (g_tab == Tab::Books) shown = notes::ForBook(g_book);   // in reading order
    }
    const int per = book_list ? 8 : 5;
    const int items = book_list ? (int)books.size() : (int)shown.size();
    const int pages = std::max(1, (items + per - 1) / per);
    g_page = std::clamp(g_page, 0, pages - 1);

    lv_obj_t* bar = Plain(p.body, LV_PCT(100), 66);
    if (g_tab == Tab::Books && !g_book.empty()) {
        lv_obj_t* back = ui::Button(bar, std::string(icon::CHEV_L) + " Sách", [] { g_book.clear(); g_page = 0; List(); }, 150, 60);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        std::string md = notes::ExportName(shown.empty() ? notes::Note{} : shown[0]);
        lv_obj_t* ex = ui::Label(bar, "Đã xuất: notes/" + md, ui::F().small);
        lv_obj_set_style_text_color(ex, kMuted, 0);
        lv_obj_set_width(ex, 440);
        lv_label_set_long_mode(ex, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(ex, LV_ALIGN_RIGHT_MID, 0, 0);
    } else {
        lv_obj_t* add = ui::Button(bar, std::string(icon::PLUS) + "  Ghi chú mới", [] { NewFree(); }, LV_PCT(100), 62);
        lv_obj_set_style_radius(add, 31, 0);
        lv_obj_set_style_border_width(add, 2, 0);
    }

    lv_obj_t* list = Plain(p.body, LV_PCT(100), 5 * 170);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    if (items == 0) {
        lv_obj_t* e = ui::Label(list, "Chưa có ghi chú. Khi đọc sách: giữ ngón tay lên một từ, chạm từ cuối để chọn cả đoạn, rồi chọn \"Trích dẫn\" hoặc \"Ghi chú\".", ui::F().serif_italic);
        lv_obj_set_width(e, LV_PCT(100));
        lv_label_set_long_mode(e, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_pad_top(e, 30, 0);
    }
    for (int i = g_page * per; i < std::min(items, (g_page + 1) * per); ++i) {
        if (!book_list) { NoteRow(list, shown[i]); continue; }
        std::string path = books[i].first;
        lv_obj_t* row = ui::Button(list, "", [path] { g_book = path; g_page = 0; List(); }, LV_PCT(100), 100);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, kHair, 0);
        lv_obj_align(ui::IconLabel(row, icon::BOOK), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* t = ui::Label(row, books[i].second.first, ui::F().serif_book);
        lv_obj_set_width(t, 430);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 60, 0);
        lv_obj_align(ui::Label(row, std::to_string(books[i].second.second) + "  " + icon::CHEV_R, ui::F().body), LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_t* pager = Plain(p.body, LV_PCT(100), 60);
    lv_obj_t* prev = ui::Button(pager, std::string(icon::CHEV_L) + " Trước", [] { g_page--; List(); }, 170, 56);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* next = ui::Button(pager, std::string("Sau ") + icon::CHEV_R, [] { g_page++; List(); }, 170, 56);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);
    if (g_page == 0) lv_obj_add_state(prev, LV_STATE_DISABLED);
    if (g_page >= pages - 1) lv_obj_add_state(next, LV_STATE_DISABLED);
    char pg[32];
    std::snprintf(pg, sizeof pg, "%d / %d", g_page + 1, pages);
    lv_obj_center(ui::Label(pager, pg, ui::F().body));
    ui::SetPageKeyHandler([pages](int dir) { int n = g_page + dir; if (n >= 0 && n < pages) { g_page = n; List(); } });
    ui::Show(p.screen);
}

}  // namespace

void OpenNotes() { List(); }
void OpenNotesForBook(const std::string& path) {
    g_tab = Tab::Books;
    g_book = path;
    g_page = 0;
    List();
}

void OpenNote(int id) {
    notes::Note n;
    if (!notes::Get(id, n)) { List(); return; }
    ui::Page p = ui::NewPage("GHI CHÚ", [] { List(); });
    lv_obj_set_style_pad_row(p.body, 16, 0);
    std::string where = n.book.empty() ? "Ghi chú riêng" : n.book_title + (n.chapter_title.empty() ? "" : " · " + n.chapter_title);
    lv_label_set_text(p.header_status, (where + " · " + Date(n.created)).c_str());
    if (!n.quote.empty()) {
        lv_obj_t* qb = lv_obj_create(p.body);
        lv_obj_set_size(qb, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(qb, 4, 0);
        lv_obj_set_style_border_side(qb, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_radius(qb, 0, 0);
        lv_obj_set_style_pad_all(qb, 0, 0);
        lv_obj_set_style_pad_left(qb, 22, 0);
        lv_obj_remove_flag(qb, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* q = ui::Label(qb, "“" + n.quote + "”", ui::F().reader(34, false));
        lv_obj_set_width(q, LV_PCT(100));
        lv_label_set_long_mode(q, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_line_space(q, 8, 0);
    }
    lv_obj_t* h = ui::Label(p.body, n.quote.empty() ? "NỘI DUNG" : "GHI CHÚ CỦA BẠN", ui::F().small);
    lv_obj_set_style_text_letter_space(h, 3, 0);
    lv_obj_set_style_text_color(h, kMuted, 0);
    lv_obj_t* t = ui::Label(p.body, n.text.empty() ? "(chưa có, bấm Sửa để thêm)" : n.text, n.text.empty() ? ui::F().serif_italic : ui::F().body);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t* row = Plain(p.body, LV_PCT(100), 80);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    if (!n.book.empty()) {
        lv_obj_t* open = ui::Button(row, std::string(icon::BOOK_TEXT) + "  Mở trong sách", [n] {
            if (!platform::FileExists(n.book)) { ui::Toast("Không thấy tệp sách trên thẻ nhớ nữa."); return; }
            OpenReaderAt(n.book, n.chapter, n.para, n.offset);
        }, 290, 76);
        ui::Invert(open, true);
    }
    ui::Button(row, std::string(icon::PEN) + "  Sửa", [n] {
        ui::PromptOptions o;
        o.title = n.quote.empty() ? "Sửa ghi chú" : "Ghi chú cho trích dẫn";
        o.initial = n.text;
        o.multiline = true;
        o.ok = "Lưu";
        ui::TextPrompt(o, [n](const std::string& txt) {
            notes::Note x = n;
            x.text = txt;
            notes::Update(x);
            OpenNote(x.id);
        });
    }, 140, 76);
    auto armed = std::make_shared<bool>(false);
    auto label = std::make_shared<lv_obj_t*>(nullptr);
    lv_obj_t* del = ui::Button(row, "", [armed, label, id] {
        if (!*armed) {   // two taps: no modal dialogs on e-ink
            *armed = true;
            if (*label) lv_label_set_text(*label, "Chắc chắn?");
            return;
        }
        notes::Remove(id);
        ui::Toast("Đã xoá ghi chú.");
        List();
    }, LV_SIZE_CONTENT, 76);
    lv_obj_set_style_pad_hor(del, 18, 0);
    *label = ui::Label(del, std::string(icon::TRASH) + " Xoá", ui::F().body);
    lv_obj_center(*label);
    ui::Show(p.screen);
}

}  // namespace screens
