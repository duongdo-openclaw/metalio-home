// Minimal EPUB 2/3 + TXT reader core: metadata, spine, table of contents, plain-text paragraphs.
// Pure C++ (zlib for deflate). On the device the firmware's own zip_reader/epub_document can
// replace the container part; the screens only use this interface.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace book {

struct TocEntry {
    std::string title;
    int spine = 0;   // chapter index this entry points to
    int depth = 0;
};

struct Paragraph {
    std::string text;
    bool heading = false;
};

class Book {
public:
    bool Open(const std::string& path, std::string& err);
    const std::string& Title() const { return title_; }
    const std::string& Author() const { return author_; }
    const std::string& Path() const { return path_; }
    int ChapterCount() const { return (int)spine_.size(); }
    const std::vector<TocEntry>& Toc() const { return toc_; }
    std::string ChapterTitle(int chapter) const;     // nearest TOC entry at or before `chapter`
    bool LoadChapter(int chapter, std::vector<Paragraph>& out, std::string& err) const;
    bool Cover(std::vector<uint8_t>& out, std::string& mime) const;  // false when the book has none
    bool IsTxt() const { return txt_; }

private:
    struct Entry { std::string name; uint16_t method = 0; uint32_t csize = 0, usize = 0, offset = 0; };
    bool ReadZip(std::string& err);
    bool Extract(const std::string& name, std::string& out) const;
    bool OpenTxt(std::string& err);

    std::string path_, title_, author_, opf_dir_, cover_href_, cover_mime_;
    std::vector<Entry> entries_;
    std::vector<std::string> spine_;      // zip entry names (epub) — unused for txt
    std::vector<TocEntry> toc_;
    bool txt_ = false;
    std::vector<std::vector<Paragraph>> txt_chapters_;
};

// XHTML -> paragraphs (exposed for tests).
void HtmlToParagraphs(const std::string& html, std::vector<Paragraph>& out);
// Resolves "a/b/../c.xhtml#x" against a base directory, %-decodes, drops the fragment.
std::string ResolvePath(const std::string& base_dir, const std::string& href);

}  // namespace book
