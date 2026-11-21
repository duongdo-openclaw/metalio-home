#include "telex.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace telex {
namespace {

// 12 vowel bases × 6 tones (none, sắc, huyền, hỏi, ngã, nặng), lower then upper case.
const char* kLower[12][6] = {
    {"a", "á", "à", "ả", "ã", "ạ"}, {"ă", "ắ", "ằ", "ẳ", "ẵ", "ặ"}, {"â", "ấ", "ầ", "ẩ", "ẫ", "ậ"},
    {"e", "é", "è", "ẻ", "ẽ", "ẹ"}, {"ê", "ế", "ề", "ể", "ễ", "ệ"}, {"i", "í", "ì", "ỉ", "ĩ", "ị"},
    {"o", "ó", "ò", "ỏ", "õ", "ọ"}, {"ô", "ố", "ồ", "ổ", "ỗ", "ộ"}, {"ơ", "ớ", "ờ", "ở", "ỡ", "ợ"},
    {"u", "ú", "ù", "ủ", "ũ", "ụ"}, {"ư", "ứ", "ừ", "ử", "ữ", "ự"}, {"y", "ý", "ỳ", "ỷ", "ỹ", "ỵ"}};
const char* kUpper[12][6] = {
    {"A", "Á", "À", "Ả", "Ã", "Ạ"}, {"Ă", "Ắ", "Ằ", "Ẳ", "Ẵ", "Ặ"}, {"Â", "Ấ", "Ầ", "Ẩ", "Ẫ", "Ậ"},
    {"E", "É", "È", "Ẻ", "Ẽ", "Ẹ"}, {"Ê", "Ế", "Ề", "Ể", "Ễ", "Ệ"}, {"I", "Í", "Ì", "Ỉ", "Ĩ", "Ị"},
    {"O", "Ó", "Ò", "Ỏ", "Õ", "Ọ"}, {"Ô", "Ố", "Ồ", "Ổ", "Ỗ", "Ộ"}, {"Ơ", "Ớ", "Ờ", "Ở", "Ỡ", "Ợ"},
    {"U", "Ú", "Ù", "Ủ", "Ũ", "Ụ"}, {"Ư", "Ứ", "Ừ", "Ử", "Ữ", "Ự"}, {"Y", "Ý", "Ỳ", "Ỷ", "Ỹ", "Ỵ"}};
enum Base { A, AW, AA, E, EE, I, O, OO, OW, U, UW, Y };

struct Cell {
    int base = -1;        // vowel base, -1 = not a vowel
    int tone = 0;
    bool upper = false;
    std::string raw;      // consonants (and đ) as typed
};

size_t CharLen(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

std::vector<Cell> Split(const std::string& w) {
    std::vector<Cell> out;
    for (size_t i = 0; i < w.size();) {
        size_t n = std::min(CharLen((unsigned char)w[i]), w.size() - i);
        std::string ch = w.substr(i, n);
        i += n;
        Cell c;
        for (int b = 0; b < 12 && c.base < 0; ++b)
            for (int t = 0; t < 6; ++t) {
                if (ch == kLower[b][t]) { c.base = b; c.tone = t; break; }
                if (ch == kUpper[b][t]) { c.base = b; c.tone = t; c.upper = true; break; }
            }
        if (c.base < 0) c.raw = ch;
        out.push_back(c);
    }
    return out;
}

std::string Join(const std::vector<Cell>& cs) {
    std::string o;
    for (const Cell& c : cs) o += c.base >= 0 ? (c.upper ? kUpper : kLower)[c.base][c.tone] : c.raw;
    return o;
}

bool IsQ(const Cell& c) { return c.base < 0 && (c.raw == "q" || c.raw == "Q"); }
bool IsG(const Cell& c) { return c.base < 0 && (c.raw == "g" || c.raw == "G"); }

// Indexes of the vowel nucleus (skipping the u of "qu" and the i of "gi" before another vowel).
std::vector<int> Nucleus(const std::vector<Cell>& cs, bool& final_consonant) {
    std::vector<int> v;
    int last = -1;
    for (int i = 0; i < (int)cs.size(); ++i) if (cs[i].base >= 0) last = i;
    final_consonant = false;
    if (last < 0) return v;
    int first = last;
    while (first > 0 && cs[first - 1].base >= 0) --first;
    for (int i = first; i <= last; ++i) v.push_back(i);
    if (v.size() > 1 && first > 0) {
        const Cell& c0 = cs[v[0]];
        if ((c0.base == U && IsQ(cs[first - 1])) || (c0.base == I && IsG(cs[first - 1]))) v.erase(v.begin());
    }
    final_consonant = last + 1 < (int)cs.size();
    return v;
}

int TonePos(const std::vector<Cell>& cs) {
    bool fin = false;
    std::vector<int> v = Nucleus(cs, fin);
    if (v.empty()) return -1;
    for (int k = (int)v.size() - 1; k >= 0; --k) {   // a hatted vowel takes the tone (ơ in ươ)
        int b = cs[v[k]].base;
        if (b == AW || b == AA || b == EE || b == OO || b == OW) return v[k];
    }
    for (int i : v) if (cs[i].base == UW && v.size() == 1) return i;
    if (v.size() == 1) return v[0];
    if (v.size() == 3) return v[1];
    return fin ? v[1] : v[0];
}

int ToneOf(char c) {
    switch (std::tolower((unsigned char)c)) {
    case 's': return 1; case 'f': return 2; case 'r': return 3; case 'x': return 4; case 'j': return 5;
    case 'z': return 0;
    default: return -1;
    }
}

}  // namespace

std::string TypeRaw(const std::string& word, char c);

// "ưo" followed by more letters is always "ươ" (chuwong → chương, nguwowif → người).
std::string Normalize(const std::string& w) {
    std::vector<Cell> cs = Split(w);
    bool changed = false;
    for (size_t i = 0; i + 2 < cs.size(); ++i)
        if (cs[i].base == UW && cs[i + 1].base == O) {
            cs[i + 1].base = OW;
            changed = true;
        }
    if (!changed) return w;
    // keep the tone on the right vowel after the change
    int tone = 0;
    for (auto& x : cs) if (x.base >= 0 && x.tone) { tone = x.tone; x.tone = 0; }
    if (tone) { int p = TonePos(cs); if (p >= 0) cs[p].tone = tone; }
    return Join(cs);
}

std::string Type(const std::string& word, char c) { return Normalize(TypeRaw(word, c)); }

std::string TypeRaw(const std::string& word, char c) {
    std::vector<Cell> cs = Split(word);
    const char lc = (char)std::tolower((unsigned char)c);
    const bool up = std::isupper((unsigned char)c);
    std::string plain(1, c);
    bool has_vowel = false;
    for (auto& x : cs) has_vowel |= x.base >= 0;

    int tone = ToneOf(c);
    if (tone >= 0 && has_vowel) {
        int pos = TonePos(cs);
        int cur = 0;
        for (auto& x : cs) if (x.base >= 0 && x.tone) cur = x.tone;
        if (tone == 0 && cur == 0) return word + plain;
        if (cur == tone) {   // same key twice: undo the tone, type the letter
            for (auto& x : cs) if (x.base >= 0) x.tone = 0;
            return Join(cs) + plain;
        }
        for (auto& x : cs) if (x.base >= 0) x.tone = 0;   // move/replace the tone
        if (pos >= 0) cs[pos].tone = tone;
        return Join(cs);
    }
    if (lc == 'd') {
        for (auto& x : cs)
            if (x.base < 0 && (x.raw == "d" || x.raw == "D")) { x.raw = x.raw == "d" ? "đ" : "Đ"; return Join(cs); }
            else if (x.base < 0 && (x.raw == "đ" || x.raw == "Đ")) { x.raw = x.raw == "đ" ? "d" : "D"; return Join(cs) + plain; }
        return word + plain;
    }
    if (lc == 'a' || lc == 'e' || lc == 'o') {   // aa ee oo: hat on the last matching vowel
        int want = lc == 'a' ? A : lc == 'e' ? E : O;
        int hat = lc == 'a' ? AA : lc == 'e' ? EE : OO;
        for (int i = (int)cs.size() - 1; i >= 0; --i) {
            if (cs[i].base < 0) { if (i < (int)cs.size() - 1 && cs[i + 1].base < 0) break; continue; }
            if (cs[i].base == want || (lc == 'a' && cs[i].base == AW) || (lc == 'o' && cs[i].base == OW)) { cs[i].base = hat; return Join(cs); }
            if (cs[i].base == hat) { cs[i].base = want; return Join(cs) + plain; }   // aaa → aa
        }
        return word + plain;
    }
    if (lc == 'w') {
        bool fin = false;
        std::vector<int> v = Nucleus(cs, fin);
        // uo → ươ (also "uow"), then the last a/o/u
        for (size_t k = 0; k + 1 < v.size(); ++k)
            if (cs[v[k]].base == U && (cs[v[k + 1]].base == O || cs[v[k + 1]].base == OW)) { cs[v[k]].base = UW; cs[v[k + 1]].base = OW; return Join(cs); }
        for (int k = (int)v.size() - 1; k >= 0; --k) {
            int& b = cs[v[k]].base;
            if (b == A || b == AA) { b = AW; return Join(cs); }
            if (b == O || b == OO) { b = OW; return Join(cs); }
            if (b == U) { b = UW; return Join(cs); }
            if (b == AW) { b = A; return Join(cs) + plain; }
            if (b == OW) { b = O; return Join(cs) + plain; }
            if (b == UW) { b = U; return Join(cs) + plain; }
        }
        if (!has_vowel) return word + (up ? "Ư" : "ư");
        return word + plain;
    }
    return word + plain;
}

std::string Convert(const std::string& ascii) {
    std::string out, word;
    for (char c : ascii) {
        if (std::isalpha((unsigned char)c)) word = Type(word, c);
        else { out += word; word.clear(); out += c; }
    }
    return out + word;
}

}  // namespace telex
