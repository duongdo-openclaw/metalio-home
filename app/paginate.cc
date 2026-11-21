#include "paginate.h"

#include <algorithm>

#include "lvgl_private.h"  // lv_text_get_next_line

namespace reader {
namespace {

int TextWidth(const char* s, size_t n, const lv_font_t* f, int letter) {
    lv_text_attributes_t a;
    lv_text_attributes_init(&a);
    a.letter_space = letter;
    a.max_width = LV_COORD_MAX;
    a.text_flags = LV_TEXT_FLAG_NONE;
    int32_t w = 0;
    // lv_text_get_width_with_flags would also work; next_line returns the used width directly.
    lv_text_get_next_line(s, (uint32_t)n, f, &w, &a);
    return (int)w;
}

}  // namespace

std::vector<Page> Paginate(const std::vector<book::Paragraph>& paras, const Layout& l) {
    std::vector<Page> pages(1);
    int y = 0;
    lv_text_attributes_t attr;
    lv_text_attributes_init(&attr);
    attr.letter_space = l.letter;
    attr.line_space = 0;
    attr.text_flags = LV_TEXT_FLAG_NONE;

    for (int pi = 0; pi < (int)paras.size(); ++pi) {
        const auto& p = paras[pi];
        const bool head = p.heading;
        const lv_font_t* font = head ? l.heading : l.body;
        const int pitch = head ? lv_font_get_line_height(l.heading) * 125 / 100 : l.line_h;
        const std::string& t = p.text;

        // Headings start on a fresh page area: extra space above (unless at the top of a page).
        if (head && y > 0) y += l.line_h;
        size_t pos = 0;
        bool first = true;
        while (pos < t.size()) {
            const int indent = (!head && first) ? l.indent : 0;
            attr.max_width = l.width - indent;
            uint32_t n = lv_text_get_next_line(t.c_str() + pos, (uint32_t)(t.size() - pos), font, nullptr, &attr);
            if (n == 0) n = (uint32_t)(t.size() - pos);
            size_t end = pos + n;
            size_t vis = end;
            while (vis > pos && (t[vis - 1] == ' ' || t[vis - 1] == '\n')) --vis;  // no trailing blanks

            if (y + pitch > l.height && !pages.back().lines.empty()) {   // page full
                pages.emplace_back();
                y = 0;
            }
            Line ln;
            ln.para = pi;
            ln.from = pos;
            ln.to = vis;
            ln.heading = head;
            ln.last = end >= t.size();
            ln.y = y + (pitch - lv_font_get_line_height(font)) / 2;
            if (head) ln.x = std::max(0, (l.width - TextWidth(t.c_str() + pos, vis - pos, font, l.letter)) / 2);
            else ln.x = indent;
            pages.back().lines.push_back(ln);
            y += pitch;
            pos = end;
            first = false;
        }
        y += head ? l.line_h / 2 : l.para_gap;
        // Keep a heading together with the text after it.
        if (head && y + l.line_h * 2 > l.height && pages.back().lines.size() > 1) {
            std::vector<Line> moved;
            auto& lines = pages.back().lines;
            while (!lines.empty() && lines.back().para == pi) { moved.insert(moved.begin(), lines.back()); lines.pop_back(); }
            pages.emplace_back();
            y = 0;
            for (auto& m : moved) {
                const int pitch2 = lv_font_get_line_height(l.heading) * 125 / 100;
                m.y = y + (pitch2 - lv_font_get_line_height(l.heading)) / 2;
                pages.back().lines.push_back(m);
                y += pitch2;
            }
            y += l.line_h / 2;
        }
    }
    if (pages.size() > 1 && pages.back().lines.empty()) pages.pop_back();
    return pages;
}

int FindPage(const std::vector<Page>& pages, int para, size_t offset) {
    for (int i = (int)pages.size() - 1; i >= 0; --i) {
        if (pages[i].lines.empty()) continue;
        const Line& f = pages[i].lines.front();
        if (f.para < para || (f.para == para && f.from <= offset)) return i;
    }
    return 0;
}

namespace {

// Justify every line that is at least 75% full; only very short lines (a couple of long words,
// no hyphenation) stay ragged because huge gaps read worse.
bool Justified(const Line& ln, const Layout& l, const std::vector<WordBox>& words, const lv_font_t* f, double& gap) {
    if (!l.justify || ln.last || ln.heading || words.size() < 2) return false;
    const int avail = l.width - ln.x;
    int sum = 0;
    for (const auto& w : words) sum += w.w;
    gap = (double)(avail - sum) / (words.size() - 1);
    const int space = TextWidth(" ", 1, f, l.letter);
    const int natural = sum + (int)(words.size() - 1) * space;
    return natural * 4 >= avail * 3 && gap >= 0;
}

bool InMark(const Mark& m, int para, size_t off) {
    if (para < m.para_from || para > m.para_to) return false;
    if (para == m.para_from && off < m.from) return false;
    if (para == m.para_to && off >= m.to) return false;
    return true;
}

}  // namespace

std::vector<WordBox> LineWords(const Line& ln, const std::vector<book::Paragraph>& paras, const Layout& l) {
    const std::string& t = paras[ln.para].text;
    const lv_font_t* f = ln.heading ? l.heading : l.body;
    const char* s = t.c_str() + ln.from;
    const size_t n = ln.to - ln.from;
    std::vector<WordBox> words;
    size_t i = 0;
    while (i < n) {
        while (i < n && s[i] == ' ') ++i;
        size_t b = i;
        while (i < n && s[i] != ' ') ++i;
        if (i > b) {
            WordBox w;
            w.from = ln.from + b;
            w.to = ln.from + i;
            w.w = TextWidth(s + b, i - b, f, l.letter);
            w.x = ln.x + TextWidth(s, b, f, l.letter) + (b ? l.letter : 0);   // natural position
            words.push_back(w);
        }
    }
    double gap = 0;
    if (Justified(ln, l, words, f, gap)) {
        double x = ln.x;
        for (auto& w : words) { w.x = (int)x; x += w.w + gap; }
    }
    return words;
}

bool HitWord(const Page& page, const std::vector<book::Paragraph>& paras, const Layout& l, int x, int y,
             int& para, size_t& from, size_t& to) {
    for (const Line& ln : page.lines) {
        const int lh = lv_font_get_line_height(ln.heading ? l.heading : l.body);
        const int slack = std::max(0, (l.line_h - lh) / 2);   // the leading between lines counts too
        if (y < ln.y - slack || y >= ln.y + lh + slack) continue;
        auto words = LineWords(ln, paras, l);
        const WordBox* best = nullptr;
        int best_d = 1 << 30;
        for (const auto& w : words) {
            int d = x < w.x ? w.x - x : x > w.x + w.w ? x - (w.x + w.w) : 0;
            if (d < best_d) { best_d = d; best = &w; }
        }
        if (!best || best_d > 24) return false;
        para = ln.para;
        from = best->from;
        to = best->to;
        return true;
    }
    return false;
}

void DrawPage(lv_layer_t* layer, const lv_area_t& o, const Page& page,
              const std::vector<book::Paragraph>& paras, const Layout& l, lv_color_t ink,
              const std::vector<Mark>& marks) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = ink;
    d.letter_space = l.letter;
    d.text_local = 1;   // LVGL copies the slice: the draw task may run after this function returns
    lv_draw_rect_dsc_t band;
    lv_draw_rect_dsc_init(&band);
    band.bg_color = lv_color_hex(0xc4c2bc);
    band.radius = 0;
    lv_draw_rect_dsc_t rule;
    lv_draw_rect_dsc_init(&rule);
    rule.bg_color = ink;
    for (const Line& ln : page.lines) {
        const std::string& t = paras[ln.para].text;
        const lv_font_t* f = ln.heading ? l.heading : l.body;
        d.font = f;
        const int lh = lv_font_get_line_height(f);
        const char* s = t.c_str() + ln.from;
        const size_t n = ln.to - ln.from;
        std::vector<WordBox> words = LineWords(ln, paras, l);

        // Selection band and highlight underlines first, text on top.
        if (!marks.empty()) {
            for (size_t k = 0; k < words.size(); ++k) {
                const WordBox& w = words[k];
                for (const Mark& m : marks) {
                    if (!InMark(m, ln.para, w.from)) continue;
                    // extend over the gap to the next word when it is marked too
                    int right = w.x + w.w;
                    if (k + 1 < words.size() && InMark(m, ln.para, words[k + 1].from)) right = words[k + 1].x;
                    if (m.selection) {
                        lv_area_t a = {o.x1 + w.x - 2, o.y1 + ln.y - 2, o.x1 + right + 1, o.y1 + ln.y + lh + 1};
                        lv_draw_rect(layer, &band, &a);
                    } else {
                        lv_area_t a = {o.x1 + w.x, o.y1 + ln.y + lh - 3, o.x1 + right - 1, o.y1 + ln.y + lh - 1};
                        lv_draw_rect(layer, &rule, &a);
                    }
                }
            }
        }

        double gap = 0;
        if (Justified(ln, l, words, f, gap)) {
            for (const WordBox& w : words) {
                std::string word(t, w.from, w.to - w.from);
                d.text = word.c_str();
                lv_area_t a = {o.x1 + w.x, o.y1 + ln.y, o.x1 + w.x + w.w + 2, o.y1 + ln.y + lh};
                lv_draw_label(layer, &d, &a);
            }
            continue;
        }
        std::string line(s, n);
        d.text = line.c_str();
        lv_area_t a = {o.x1 + ln.x, o.y1 + ln.y, o.x1 + l.width + 4, o.y1 + ln.y + lh};
        lv_draw_label(layer, &d, &a);
    }
}

std::string PageText(const Page& page, const std::vector<book::Paragraph>& paras, size_t max_bytes) {
    std::string out;
    for (const Line& ln : page.lines) {
        if (!out.empty()) out += ' ';
        out += paras[ln.para].text.substr(ln.from, ln.to - ln.from);
        if (out.size() >= max_bytes) break;
    }
    if (out.size() > max_bytes) {
        size_t cut = max_bytes;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) --cut;  // keep UTF-8 whole
        out = out.substr(0, cut) + "…";
    }
    return out;
}

}  // namespace reader
