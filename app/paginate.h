// Line-level page layout for the reader (like Kreader's own line breaker): LVGL measures, we place.
// Supports justified text, pixel first-line indent, line height in %, letter spacing, paragraph
// spacing and centred headings. The page widget (DrawPage) renders exactly these lines.
#pragma once
#include <string>
#include <vector>

#include "book.h"
#include "lvgl.h"

namespace reader {

struct Layout {
    const lv_font_t* body = nullptr;
    const lv_font_t* heading = nullptr;
    int width = 0, height = 0;
    int line_h = 0;          // body line pitch in px (font height * line %)
    int letter = 0;          // extra px between letters
    int indent = 0;          // first-line indent in px (0 = none)
    int para_gap = 0;        // extra px between paragraphs
    bool justify = true;
};

struct Line {
    int para = 0;
    size_t from = 0, to = 0; // byte range in the paragraph text (trailing spaces excluded)
    int x = 0, y = 0;        // top-left inside the page
    bool last = false;       // last line of its paragraph (never justified)
    bool heading = false;
};

struct Page {
    std::vector<Line> lines;
};

std::vector<Page> Paginate(const std::vector<book::Paragraph>& paras, const Layout& l);
int FindPage(const std::vector<Page>& pages, int para, size_t offset);

// Where every word of a line is drawn (same rule as DrawPage, justified or not).
struct WordBox {
    size_t from = 0, to = 0;   // byte range in the paragraph
    int x = 0, w = 0;          // inside the page
};
std::vector<WordBox> LineWords(const Line& ln, const std::vector<book::Paragraph>& paras, const Layout& l);

// A range of text on the page: saved highlights (notes) are underlined, the live selection is
// drawn on a grey band.
struct Mark {
    int para_from = 0;
    size_t from = 0;
    int para_to = 0;
    size_t to = 0;             // exclusive
    bool selection = false;
};

// Word under (x, y) inside the page; false when the point is between lines or past the text.
bool HitWord(const Page& page, const std::vector<book::Paragraph>& paras, const Layout& l, int x, int y,
             int& para, size_t& from, size_t& to);

// Draws one page into an LVGL layer (call from LV_EVENT_DRAW_MAIN of the page object).
void DrawPage(lv_layer_t* layer, const lv_area_t& origin, const Page& page,
              const std::vector<book::Paragraph>& paras, const Layout& l, lv_color_t ink,
              const std::vector<Mark>& marks = {});

// Plain text of a page (for bookmarks' snippets and search previews).
std::string PageText(const Page& page, const std::vector<book::Paragraph>& paras, size_t max_bytes);

}  // namespace reader
