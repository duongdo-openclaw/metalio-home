#include "book.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include "drive_folder.h"  // DecodeEntities

namespace book {
namespace {

uint16_t U16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t U32(const unsigned char* p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Value of attribute `name` inside one tag "<item id=.. href=..>".
std::string Attr(const std::string& tag, const char* name) {
    std::string key = std::string(name) + "=";
    size_t p = 0;
    while ((p = tag.find(key, p)) != std::string::npos) {
        if (p > 0 && !std::isspace((unsigned char)tag[p - 1])) { p += key.size(); continue; }
        size_t v = p + key.size();
        if (v >= tag.size()) return "";
        char q = tag[v];
        if (q != '"' && q != '\'') return "";
        size_t e = tag.find(q, v + 1);
        return e == std::string::npos ? "" : drive::DecodeEntities(tag.substr(v + 1, e - v - 1));
    }
    return "";
}

// Iterates tags named `name` (with or without a namespace prefix): calls fn(tag_text, inner_start).
template <class F>
void EachTag(const std::string& xml, const char* name, F fn) {
    size_t p = 0;
    const std::string n = name;
    while ((p = xml.find('<', p)) != std::string::npos) {
        size_t e = xml.find('>', p);
        if (e == std::string::npos) return;
        size_t s = p + 1;
        size_t ne = s;
        while (ne < e && !std::isspace((unsigned char)xml[ne]) && xml[ne] != '/' && xml[ne] != '>') ++ne;
        std::string tn = xml.substr(s, ne - s);
        size_t colon = tn.find(':');
        if (colon != std::string::npos) tn = tn.substr(colon + 1);
        if (tn == n) fn(xml.substr(p, e - p + 1), e + 1);
        p = e + 1;
    }
}

std::string InnerText(const std::string& xml, size_t start, const char* close_name) {
    size_t e = xml.find(std::string("</") + close_name, start);
    if (e == std::string::npos) {
        // namespaced close tag, e.g. </dc:title>
        e = xml.find("</", start);
        if (e == std::string::npos) return "";
    }
    std::string raw = xml.substr(start, e - start);
    std::string out;
    bool in_tag = false;
    for (char c : raw) {
        if (c == '<') in_tag = true;
        else if (c == '>') in_tag = false;
        else if (!in_tag) out += c;
    }
    return Trim(drive::DecodeEntities(out));
}

std::string PercentDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::string Dir(const std::string& path) {
    size_t s = path.rfind('/');
    return s == std::string::npos ? "" : path.substr(0, s + 1);
}

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool IsChapterHeading(const std::string& line) {
    if (line.size() > 90 || line.empty()) return false;
    static const char* prefixes[] = {"Chương ", "CHƯƠNG ", "Chapter ", "CHAPTER ", "Hồi ", "HỒI ", "Phần ", "PHẦN "};
    for (const char* p : prefixes)
        if (line.rfind(p, 0) == 0) return true;
    return false;
}

}  // namespace

std::string ResolvePath(const std::string& base_dir, const std::string& href) {
    std::string h = PercentDecode(href);
    size_t hash = h.find('#');
    if (hash != std::string::npos) h = h.substr(0, hash);
    std::string full = h.rfind('/', 0) == 0 ? h.substr(1) : base_dir + h;
    std::vector<std::string> parts;
    std::stringstream ss(full);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); continue; }
        parts.push_back(seg);
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
    return out;
}

void HtmlToParagraphs(const std::string& html, std::vector<Paragraph>& out) {
    static const char* kBlock[] = {"p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6", "li", "tr",
                                   "blockquote", "section", "article", "dt", "dd", "hr", "pre", "table"};
    size_t body = Lower(html.substr(0, std::min<size_t>(html.size(), 4096))).find("<body");
    size_t p = body == std::string::npos ? 0 : body;
    std::string cur;
    bool heading = false;
    auto flush = [&]() {
        std::string t;
        bool space = false;
        for (char c : cur) {  // collapse whitespace
            if (std::isspace((unsigned char)c)) { space = !t.empty(); continue; }
            if (space) { t += ' '; space = false; }
            t += c;
        }
        t = drive::DecodeEntities(t);
        // drop non-breaking-space-only paragraphs
        if (!t.empty() && t != "\xC2\xA0") out.push_back({t, heading});
        cur.clear();
        heading = false;
    };
    while (p < html.size()) {
        char c = html[p];
        if (c != '<') { cur += c; ++p; continue; }
        size_t e = html.find('>', p);
        if (e == std::string::npos) break;
        std::string tag = Lower(html.substr(p + 1, e - p - 1));
        bool closing = !tag.empty() && tag[0] == '/';
        size_t ns = closing ? 1 : 0, ne = ns;
        while (ne < tag.size() && !std::isspace((unsigned char)tag[ne]) && tag[ne] != '/') ++ne;
        std::string name = tag.substr(ns, ne - ns);
        if (!closing && (name == "script" || name == "style" || name == "head")) {
            size_t close = Lower(html.substr(e)).find("</" + name);
            p = close == std::string::npos ? html.size() : e + close;
            continue;
        }
        for (const char* b : kBlock) {
            if (name == b) {
                flush();
                if (!closing && name.size() == 2 && name[0] == 'h' && std::isdigit((unsigned char)name[1])) heading = true;
                break;
            }
        }
        if (name == "img" && !closing) {
            std::string alt = Attr(html.substr(p, e - p + 1), "alt");
            (void)alt;  // images are not rendered yet
        }
        p = e + 1;
    }
    flush();
}

bool Book::ReadZip(std::string& err) {
    std::ifstream f(path_, std::ios::binary | std::ios::ate);
    if (!f) { err = "Không mở được tệp"; return false; }
    std::streamoff size = f.tellg();
    std::streamoff tail = std::min<std::streamoff>(size, 65536 + 22);
    std::vector<unsigned char> buf((size_t)tail);
    f.seekg(size - tail);
    f.read((char*)buf.data(), tail);
    long eocd = -1;
    for (long i = (long)tail - 22; i >= 0; --i)
        if (U32(&buf[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { err = "Không phải tệp EPUB (zip hỏng)"; return false; }
    uint16_t count = U16(&buf[eocd + 10]);
    uint32_t cd_size = U32(&buf[eocd + 12]), cd_off = U32(&buf[eocd + 16]);
    std::vector<unsigned char> cd(cd_size);
    f.seekg(cd_off);
    f.read((char*)cd.data(), cd_size);
    size_t p = 0;
    for (int i = 0; i < count && p + 46 <= cd.size(); ++i) {
        if (U32(&cd[p]) != 0x02014b50) break;
        Entry e;
        e.method = U16(&cd[p + 10]);
        e.csize = U32(&cd[p + 20]);
        e.usize = U32(&cd[p + 24]);
        uint16_t nlen = U16(&cd[p + 28]), xlen = U16(&cd[p + 30]), clen = U16(&cd[p + 32]);
        e.offset = U32(&cd[p + 42]);
        e.name.assign((const char*)&cd[p + 46], nlen);
        entries_.push_back(e);
        p += 46 + nlen + xlen + clen;
    }
    return !entries_.empty();
}

bool Book::Extract(const std::string& name, std::string& out) const {
    const Entry* e = nullptr;
    for (const auto& x : entries_) if (x.name == name) { e = &x; break; }
    if (!e) {  // some books differ in case
        std::string ln = Lower(name);
        for (const auto& x : entries_) if (Lower(x.name) == ln) { e = &x; break; }
    }
    if (!e || e->usize > 32u * 1024 * 1024) return false;
    std::ifstream f(path_, std::ios::binary);
    unsigned char lh[30];
    f.seekg(e->offset);
    if (!f.read((char*)lh, 30) || U32(lh) != 0x04034b50) return false;
    f.seekg(e->offset + 30 + U16(lh + 26) + U16(lh + 28));
    std::string comp(e->csize, '\0');
    if (!f.read(&comp[0], e->csize)) return false;
    if (e->method == 0) { out = comp; return true; }
    if (e->method != 8) return false;
    out.assign(e->usize, '\0');
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = (Bytef*)comp.data();
    zs.avail_in = (uInt)comp.size();
    zs.next_out = (Bytef*)&out[0];
    zs.avail_out = (uInt)out.size();
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return rc == Z_STREAM_END;
}

bool Book::OpenTxt(std::string& err) {
    std::string data;
    if (!ReadFile(path_, data)) { err = "Không mở được tệp"; return false; }
    if (data.rfind("\xEF\xBB\xBF", 0) == 0) data = data.substr(3);
    txt_ = true;
    size_t slash = path_.rfind('/');
    title_ = path_.substr(slash == std::string::npos ? 0 : slash + 1);
    if (title_.size() > 4) title_ = title_.substr(0, title_.size() - 4);
    std::vector<Paragraph> cur;
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty()) continue;
        bool head = IsChapterHeading(line);
        if ((head && !cur.empty()) || cur.size() >= 300) {
            txt_chapters_.push_back(cur);
            cur.clear();
        }
        if (head) toc_.push_back({line, (int)txt_chapters_.size(), 0});
        cur.push_back({line, head});
    }
    if (!cur.empty()) txt_chapters_.push_back(cur);
    spine_.assign(txt_chapters_.size(), "");
    if (spine_.empty()) { err = "Tệp trống"; return false; }
    return true;
}

bool Book::Open(const std::string& path, std::string& err) {
    *this = Book();
    path_ = path;
    std::string lower = Lower(path);
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".txt") return OpenTxt(err);
    if (!ReadZip(err)) return false;

    std::string container, opf;
    if (!Extract("META-INF/container.xml", container)) { err = "EPUB thiếu container.xml"; return false; }
    std::string opf_path;
    EachTag(container, "rootfile", [&](const std::string& tag, size_t) {
        if (opf_path.empty()) opf_path = Attr(tag, "full-path");
    });
    if (opf_path.empty() || !Extract(opf_path, opf)) { err = "EPUB thiếu tệp OPF"; return false; }
    opf_dir_ = Dir(opf_path);

    EachTag(opf, "title", [&](const std::string&, size_t s) { if (title_.empty()) title_ = InnerText(opf, s, "dc:title"); });
    EachTag(opf, "creator", [&](const std::string&, size_t s) { if (author_.empty()) author_ = InnerText(opf, s, "dc:creator"); });

    struct Item { std::string href, type, props; };
    std::map<std::string, Item> manifest;
    std::string cover_id, nav_href, ncx_href;
    EachTag(opf, "item", [&](const std::string& tag, size_t) {
        Item it{Attr(tag, "href"), Attr(tag, "media-type"), Attr(tag, "properties")};
        std::string id = Attr(tag, "id");
        manifest[id] = it;
        if (it.props.find("cover-image") != std::string::npos) cover_id = id;
        if (it.props.find("nav") != std::string::npos) nav_href = it.href;
        if (it.type == "application/x-dtbncx+xml") ncx_href = it.href;
    });
    EachTag(opf, "meta", [&](const std::string& tag, size_t) {
        if (cover_id.empty() && Attr(tag, "name") == "cover") cover_id = Attr(tag, "content");
    });
    if (!cover_id.empty() && manifest.count(cover_id)) {
        cover_href_ = ResolvePath(opf_dir_, manifest[cover_id].href);
        cover_mime_ = manifest[cover_id].type;
    }
    EachTag(opf, "itemref", [&](const std::string& tag, size_t) {
        auto it = manifest.find(Attr(tag, "idref"));
        if (it != manifest.end() && Attr(tag, "linear") != "no") spine_.push_back(ResolvePath(opf_dir_, it->second.href));
    });
    if (spine_.empty()) { err = "EPUB không có nội dung (spine trống)"; return false; }
    if (title_.empty() || Lower(title_).find(".epub") != std::string::npos) {
        title_ = path.substr(path.rfind('/') + 1);
        if (Lower(title_).size() > 5 && Lower(title_).substr(title_.size() - 5) == ".epub") title_.resize(title_.size() - 5);
    }

    auto spine_index = [&](const std::string& full) {
        for (size_t i = 0; i < spine_.size(); ++i) if (spine_[i] == full) return (int)i;
        return -1;
    };
    std::string toc_xml;
    if (!ncx_href.empty() && Extract(ResolvePath(opf_dir_, ncx_href), toc_xml)) {
        std::string ncx_dir = Dir(ResolvePath(opf_dir_, ncx_href));
        // navPoint nesting depth = count of open navPoints before this one minus closes.
        size_t p = 0;
        int depth = -1;
        while ((p = toc_xml.find('<', p)) != std::string::npos) {
            if (toc_xml.compare(p, 9, "<navPoint") == 0) ++depth;
            else if (toc_xml.compare(p, 11, "</navPoint>") == 0) --depth;
            else if (toc_xml.compare(p, 5, "<text") == 0 && depth >= 0) {
                size_t gt = toc_xml.find('>', p);
                std::string title = InnerText(toc_xml, gt + 1, "text");
                size_t c = toc_xml.find("<content", gt);
                if (c != std::string::npos) {
                    std::string src = Attr(toc_xml.substr(c, toc_xml.find('>', c) - c + 1), "src");
                    int si = spine_index(ResolvePath(ncx_dir, src));
                    if (si >= 0 && !title.empty()) toc_.push_back({title, si, depth});
                }
            }
            ++p;
        }
    } else if (!nav_href.empty() && Extract(ResolvePath(opf_dir_, nav_href), toc_xml)) {
        std::string nav_dir = Dir(ResolvePath(opf_dir_, nav_href));
        size_t toc_at = toc_xml.find("toc");
        std::string part = toc_at == std::string::npos ? toc_xml : toc_xml.substr(toc_at);
        EachTag(part, "a", [&](const std::string& tag, size_t s) {
            int si = spine_index(ResolvePath(nav_dir, Attr(tag, "href")));
            std::string title = InnerText(part, s, "a");
            if (si >= 0 && !title.empty()) toc_.push_back({title, si, 0});
        });
    }
    return true;
}

std::string Book::ChapterTitle(int chapter) const {
    std::string best;
    for (const auto& t : toc_) if (t.spine <= chapter) best = t.title;
    if (best.empty()) best = toc_.empty() ? Title() : "Mở đầu";  // pages before the first TOC entry
    return best;
}

bool Book::LoadChapter(int chapter, std::vector<Paragraph>& out, std::string& err) const {
    out.clear();
    if (chapter < 0 || chapter >= ChapterCount()) { err = "Sai chương"; return false; }
    if (txt_) { out = txt_chapters_[chapter]; return true; }
    std::string html;
    if (!Extract(spine_[chapter], html)) { err = "Không đọc được " + spine_[chapter]; return false; }
    HtmlToParagraphs(html, out);
    return true;
}

bool Book::Cover(std::vector<uint8_t>& out, std::string& mime) const {
    if (txt_ || cover_href_.empty()) return false;
    std::string data;
    if (!Extract(cover_href_, data)) return false;
    out.assign(data.begin(), data.end());
    mime = cover_mime_;
    return true;
}

}  // namespace book
