// Accent-insensitive search and the line layout invariants (no LVGL display needed for search;
// pagination uses a built-in LVGL font, so LVGL is initialised without a display).
#include "check.h"
#include "lvgl.h"
#include "paginate.h"
#include "text_search.h"

int main() {
    // --- search ---
    CHECK(search::Fold("Không Dấu ĐƯỜNG") == "khong dau duong");
    std::string t = "Trăm năm trong cõi người ta, chữ tài chữ mệnh khéo là ghét nhau.";
    auto h = search::FindAll(t, "chu tai");
    CHECK(h.size() == 1);
    if (!h.empty()) CHECK(t.compare(h[0], std::string("chữ tài").size(), "chữ tài") == 0);   // offset points at the original bytes
    CHECK(search::FindAll(t, "CHỮ").size() == 2);
    CHECK(search::FindAll(t, "   ").empty());
    CHECK(search::FindAll(t, "khong co").empty());

    // --- pagination ---
    lv_init();
    std::vector<book::Paragraph> paras;
    paras.push_back({"Chương một", true});
    std::string longp;
    for (int i = 0; i < 60; ++i) longp += "Lorem ipsum dolor sit amet consectetur ";
    for (int i = 0; i < 6; ++i) paras.push_back({longp, false});
    reader::Layout l;
    l.body = &lv_font_montserrat_28;
    l.heading = &lv_font_montserrat_36;
    l.width = 600;
    l.height = 900;
    l.line_h = 42;
    l.indent = 40;
    l.para_gap = 0;
    auto pages = reader::Paginate(paras, l);
    CHECK(pages.size() > 3);
    size_t covered = 0;
    int prev_para = 0;
    size_t prev_to = 0;
    bool ordered = true, fits = true;
    for (const auto& p : pages) {
        for (const auto& ln : p.lines) {
            if (ln.y + lv_font_get_line_height(ln.heading ? l.heading : l.body) > l.height + 1) fits = false;
            if (ln.para < prev_para || (ln.para == prev_para && ln.from < prev_to)) ordered = false;
            prev_para = ln.para;
            prev_to = ln.to;
            covered += ln.to - ln.from;
        }
    }
    CHECK(fits);      // every line inside the page height
    CHECK(ordered);   // text flows forward, nothing repeated
    CHECK(covered > 6 * longp.size() * 9 / 10);   // all text placed (only blanks between lines dropped)
    CHECK(pages[0].lines[0].heading && pages[0].lines[0].x > 0);   // heading centred
    CHECK(pages[0].lines[1].x == 40);                               // first body line indented
    // Restoring a position finds the page that contains it.
    const auto& mid = pages[2].lines[3];
    CHECK(reader::FindPage(pages, mid.para, mid.from) == 2);
    DONE();
}
