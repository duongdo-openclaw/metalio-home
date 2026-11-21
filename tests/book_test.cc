#include <cstring>

#include "book.h"
#include "check.h"

int main() {
    book::Book b;
    std::string err;
    CHECK(b.Open(std::string(FIXTURES_DIR) + "/sample.epub", err));
    CHECK(b.Title() == "Truyện thử & kiểm tra");
    CHECK(b.Author() == "Tác Giả A");
    CHECK(b.ChapterCount() == 2);  // linear="no" item skipped
    CHECK(b.Toc().size() == 3);
    CHECK(b.Toc().size() == 3 && b.Toc()[1].depth == 1 && b.Toc()[1].spine == 0);  // %20 + #fragment resolved
    CHECK(b.ChapterTitle(1) == "Chương 2");

    std::vector<book::Paragraph> ps;
    CHECK(b.LoadChapter(0, ps, err));
    CHECK(ps.size() == 4);
    if (ps.size() == 4) {
        CHECK(ps[0].heading && ps[0].text == "Chương 1");
        CHECK(ps[1].text == "Dòng đầu tiên ế & đậm.");  // whitespace collapsed, entities decoded
        CHECK(ps[2].text == "Đoạn hai.");               // nbsp-only paragraph dropped, <br> splits
        CHECK(ps[3].text == "Dòng mới");                // <script> dropped
    }
    std::vector<uint8_t> cover;
    std::string mime;
    CHECK(b.Cover(cover, mime) && mime == "image/jpeg" && cover.size() == 10);

    CHECK(book::ResolvePath("OEBPS/text/", "../img/a%20b.jpg#x") == "OEBPS/img/a b.jpg");

    book::Book t;
    CHECK(t.Open(std::string(FIXTURES_DIR) + "/sample.txt", err));
    CHECK(t.IsTxt() && t.Title() == "sample");
    CHECK(t.ChapterCount() == 3);
    CHECK(t.Toc().size() == 2 && t.Toc()[0].title == "Chương 1 Gặp gỡ" && t.Toc()[0].spine == 1);
    CHECK(t.LoadChapter(0, ps, err) && ps.size() == 1 && ps[0].text == "Lời nói đầu");  // BOM stripped

    book::Book bad;
    CHECK(!bad.Open(std::string(FIXTURES_DIR) + "/drive_folder.html", err));
    DONE();
}
