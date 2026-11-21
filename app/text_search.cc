#include "text_search.h"

#include <cstdint>
#include <cstring>

namespace search {
namespace {

// Decodes one UTF-8 code point; returns its length (1 on malformed input).
size_t Decode(const std::string& s, size_t i, uint32_t& cp) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { cp = c; return 1; }
    size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (n == 1 || i + n > s.size()) { cp = c; return 1; }
    cp = c & (0xFF >> (n + 1));
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    return n;
}

// Base letter of Latin / Vietnamese characters; 0 = keep as is.
char Base(uint32_t cp) {
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') ? (char)(cp + 32) : (char)cp;
    // Vietnamese letters with diacritics, grouped by base letter (lower and upper case).
    static const struct { const char* chars; char base; } groups[] = {
        {"àáảãạăằắẳẵặâầấẩẫậÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬ", 'a'},
        {"èéẻẽẹêềếểễệÈÉẺẼẸÊỀẾỂỄỆ", 'e'},
        {"ìíỉĩịÌÍỈĨỊ", 'i'},
        {"òóỏõọôồốổỗộơờớởỡợÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢ", 'o'},
        {"ùúủũụưừứửữựÙÚỦŨỤƯỪỨỬỮỰ", 'u'},
        {"ỳýỷỹỵỲÝỶỸỴ", 'y'},
        {"đĐ", 'd'},
        {"çÇ", 'c'}, {"ñÑ", 'n'},
    };
    for (const auto& g : groups) {
        std::string str = g.chars;
        size_t i = 0;
        while (i < str.size()) {
            uint32_t c;
            i += Decode(str, i, c);
            if (c == cp) return g.base;
        }
    }
    if (cp == 0x00A0 || cp == 0x2009 || cp == 0x202F) return ' ';
    return 0;
}

}  // namespace

std::string Fold(const std::string& s, std::vector<size_t>* map) {
    std::string out;
    out.reserve(s.size());
    if (map) map->clear();
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp;
        size_t n = Decode(s, i, cp);
        char b = Base(cp);
        if (b) {
            out += b;
            if (map) map->push_back(i);
        } else {
            for (size_t k = 0; k < n; ++k) {
                out += s[i + k];
                if (map) map->push_back(i);
            }
        }
        i += n;
    }
    return out;
}

std::vector<size_t> FindAll(const std::string& text, const std::string& query) {
    std::vector<size_t> hits;
    std::string q = Fold(query);
    while (!q.empty() && q.back() == ' ') q.pop_back();
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    if (q.empty()) return hits;
    std::vector<size_t> map;
    std::string t = Fold(text, &map);
    size_t p = 0;
    while ((p = t.find(q, p)) != std::string::npos) {
        hits.push_back(map[p]);
        p += q.size();
    }
    return hits;
}

}  // namespace search
