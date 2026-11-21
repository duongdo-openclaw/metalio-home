#include "media_info.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace media {
namespace {

void AppendUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

std::string DecodeText(const unsigned char* p, size_t n) {
    if (n == 0) return "";
    int enc = p[0];
    ++p; --n;
    std::string o;
    if (enc == 0) {  // ISO-8859-1
        for (size_t i = 0; i < n && p[i]; ++i) AppendUtf8(o, p[i]);
    } else if (enc == 3) {  // UTF-8
        for (size_t i = 0; i < n && p[i]; ++i) o += (char)p[i];
    } else {  // 1 = UTF-16 with BOM, 2 = UTF-16BE
        bool be = enc == 2;
        size_t i = 0;
        if (enc == 1 && n >= 2) { be = p[0] == 0xFE && p[1] == 0xFF; i = 2; }
        for (; i + 1 < n; i += 2) {
            uint32_t cu = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
            if (cu == 0) break;
            if (cu >= 0xD800 && cu <= 0xDFFF) continue;  // drop surrogates (no emoji on e-ink)
            AppendUtf8(o, cu);
        }
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '\0')) o.pop_back();
    return o;
}

}  // namespace

Info Read(const std::string& path) {
    Info info;
    std::ifstream f(path, std::ios::binary);
    if (!f) return info;
    unsigned char h[44] = {};
    f.read((char*)h, 44);
    if (std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WAVE", 4) == 0) {
        uint32_t rate = h[24] | h[25] << 8 | h[26] << 16 | (uint32_t)h[27] << 24;
        uint16_t ch = h[22] | h[23] << 8, bits = h[34] | h[35] << 8;
        f.seekg(0, std::ios::end);
        uint64_t data = (uint64_t)f.tellg() - 44;
        uint64_t bps = (uint64_t)rate * ch * (bits / 8);
        if (bps) info.duration_ms = data * 1000 / bps;
        return info;
    }
    if (std::memcmp(h, "ID3", 3) != 0) return info;
    int ver = h[3];
    uint32_t size = (h[6] & 0x7F) << 21 | (h[7] & 0x7F) << 14 | (h[8] & 0x7F) << 7 | (h[9] & 0x7F);
    if (size > 4 * 1024 * 1024) return info;
    std::vector<unsigned char> tag(size);
    f.seekg(10);
    f.read((char*)tag.data(), size);
    size_t p = 0;
    while (p + 10 <= tag.size() && tag[p]) {
        std::string id((const char*)&tag[p], 4);
        uint32_t fs = ver >= 4 ? ((tag[p + 4] & 0x7F) << 21 | (tag[p + 5] & 0x7F) << 14 | (tag[p + 6] & 0x7F) << 7 | (tag[p + 7] & 0x7F))
                               : (tag[p + 4] << 24 | tag[p + 5] << 16 | tag[p + 6] << 8 | tag[p + 7]);
        if (fs == 0 || p + 10 + fs > tag.size()) break;
        if (id == "TIT2") info.title = DecodeText(&tag[p + 10], fs);
        else if (id == "TPE1") info.artist = DecodeText(&tag[p + 10], fs);
        p += 10 + fs;
    }
    return info;
}

std::string DisplayName(const std::string& name) {
    std::string s = name.substr(0, name.rfind('.'));
    for (auto& c : s) if (c == '_') c = ' ';
    return s;
}

}  // namespace media
