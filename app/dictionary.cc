#include "dictionary.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

#include "drive_folder.h"   // DecodeEntities
#include "platform.h"

namespace dict {
namespace {

std::string Lower(const std::string& s) {
    std::string o = s;
    for (auto& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}

// StarDict sorts with g_ascii_strcasecmp, then strcmp to break ties.
int CaseCmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        int ca = std::tolower((unsigned char)*a), cb = std::tolower((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
}

bool StartsCase(const char* s, const std::string& prefix) {
    for (size_t i = 0; i < prefix.size(); ++i)
        if (!s[i] || std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i])) return false;
    return true;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// ---------- dictzip (.dict.dz): gzip with a random-access chunk table in the FEXTRA "RA" field ----------
class DictData {
public:
    bool Open(const std::string& path, std::string& err) {
        path_ = path;
        dz_ = path.size() > 3 && Lower(path.substr(path.size() - 3)) == ".dz";
        if (!dz_) return platform::FileExists(path) || (err = "thiếu tệp .dict", false);
        std::string h;
        if (!platform::ReadRange(path, 0, 65536, h) || h.size() < 18 || (uint8_t)h[0] != 0x1f || (uint8_t)h[1] != 0x8b) {
            err = "tệp .dict.dz hỏng";
            return false;
        }
        const uint8_t flg = (uint8_t)h[3];
        size_t pos = 10;
        if (flg & 4) {   // FEXTRA
            size_t xlen = (uint8_t)h[pos] | ((uint8_t)h[pos + 1] << 8);
            size_t x = pos + 2, end = x + xlen;
            while (x + 4 <= end && end <= h.size()) {
                size_t slen = (uint8_t)h[x + 2] | ((uint8_t)h[x + 3] << 8);
                if (h[x] == 'R' && h[x + 1] == 'A' && x + 4 + slen <= end) {
                    const uint8_t* p = (const uint8_t*)h.data() + x + 4;
                    chunk_len_ = p[2] | (p[3] << 8);
                    size_t count = p[4] | (p[5] << 8);
                    for (size_t i = 0; i < count && 6 + 2 * i + 1 < slen; ++i) sizes_.push_back(p[6 + 2 * i] | (p[7 + 2 * i] << 8));
                }
                x += 4 + slen;
            }
            pos = end;
        }
        if (flg & 8) { while (pos < h.size() && h[pos]) ++pos; ++pos; }   // FNAME
        if (flg & 16) { while (pos < h.size() && h[pos]) ++pos; ++pos; }  // FCOMMENT
        if (flg & 2) pos += 2;                                             // FHCRC
        data_start_ = pos;
        if (sizes_.empty() || !chunk_len_) {
            // Plain gzip: no random access. Inflate it once (only sensible for small dictionaries).
            std::string all;
            if (!platform::ReadText(path, all) || all.size() > 16u * 1024 * 1024) { err = ".dict.dz không có bảng chunk (dictzip)"; return false; }
            if (!Gunzip(all, whole_)) { err = "giải nén .dict.dz lỗi"; return false; }
            have_whole_ = true;
        }
        offsets_.resize(sizes_.size() + 1, data_start_);
        for (size_t i = 0; i < sizes_.size(); ++i) offsets_[i + 1] = offsets_[i] + sizes_[i];
        return true;
    }

    bool Read(uint64_t off, uint32_t len, std::string& out) {
        if (!dz_) return platform::ReadRange(path_, off, len, out) && out.size() == len;
        if (have_whole_) {
            if (off + len > whole_.size()) return false;
            out = whole_.substr(off, len);
            return true;
        }
        out.clear();
        size_t first = off / chunk_len_, last = (off + len - 1) / chunk_len_;
        if (last >= sizes_.size()) return false;
        std::string plain;
        for (size_t c = first; c <= last; ++c) {
            std::string chunk;
            if (!Chunk(c, chunk)) return false;
            plain += chunk;
        }
        size_t skip = off - first * chunk_len_;
        if (skip + len > plain.size()) return false;
        out = plain.substr(skip, len);
        return true;
    }

private:
    bool Chunk(size_t c, std::string& out) {
        auto it = cache_.find(c);
        if (it != cache_.end()) { out = it->second; return true; }
        std::string comp;
        if (!platform::ReadRange(path_, offsets_[c], sizes_[c], comp)) return false;
        z_stream z{};
        if (inflateInit2(&z, -15) != Z_OK) return false;
        out.assign(chunk_len_, '\0');
        z.next_in = (Bytef*)comp.data();
        z.avail_in = (uInt)comp.size();
        z.next_out = (Bytef*)&out[0];
        z.avail_out = (uInt)out.size();
        int r = inflate(&z, Z_SYNC_FLUSH);
        size_t got = out.size() - z.avail_out;
        inflateEnd(&z);
        if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) return false;
        out.resize(got);
        if (cache_.size() > 8) cache_.clear();
        cache_[c] = out;
        return true;
    }

    static bool Gunzip(const std::string& in, std::string& out) {
        z_stream z{};
        if (inflateInit2(&z, 16 + 15) != Z_OK) return false;
        z.next_in = (Bytef*)in.data();
        z.avail_in = (uInt)in.size();
        char buf[65536];
        int r;
        do {
            z.next_out = (Bytef*)buf;
            z.avail_out = sizeof buf;
            r = inflate(&z, Z_NO_FLUSH);
            out.append(buf, sizeof buf - z.avail_out);
        } while (r == Z_OK);
        inflateEnd(&z);
        return r == Z_STREAM_END;
    }

    std::string path_;
    bool dz_ = false, have_whole_ = false;
    std::string whole_;
    size_t chunk_len_ = 0, data_start_ = 0;
    std::vector<uint32_t> sizes_;
    std::vector<uint64_t> offsets_;
    std::map<size_t, std::string> cache_;
};

class StarDict : public Dictionary {
public:
    bool Open(const std::string& ifo, std::string& err) {
        std::string info;
        if (!platform::ReadText(ifo, info) || info.find("StarDict's dict ifo file") == std::string::npos) { err = "không phải tệp .ifo của StarDict"; return false; }
        std::map<std::string, std::string> kv;
        size_t p = 0;
        while (p < info.size()) {
            size_t e = info.find('\n', p);
            std::string line = Trim(info.substr(p, e == std::string::npos ? std::string::npos : e - p));
            size_t eq = line.find('=');
            if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
            if (e == std::string::npos) break;
            p = e + 1;
        }
        name_ = kv.count("bookname") ? kv["bookname"] : ifo.substr(ifo.rfind('/') + 1);
        types_ = kv["sametypesequence"];
        off64_ = kv["idxoffsetbits"] == "64";
        std::string base = ifo.substr(0, ifo.size() - 4);
        if (!platform::ReadText(base + ".idx", idx_)) {
            std::string gz;
            if (!platform::ReadText(base + ".idx.gz", gz) || !Gunzip(gz, idx_)) { err = "thiếu tệp .idx"; return false; }
        }
        std::string e2;
        if (!data_.Open(platform::FileExists(base + ".dict.dz") ? base + ".dict.dz" : base + ".dict", e2)) { err = e2; return false; }
        const size_t tail = off64_ ? 12 : 8;
        for (size_t q = 0; q < idx_.size();) {
            size_t z = idx_.find('\0', q);
            if (z == std::string::npos || z + 1 + tail > idx_.size()) break;
            entries_.push_back((uint32_t)q);
            q = z + 1 + tail;
        }
        if (entries_.empty()) { err = "từ điển rỗng"; return false; }
        return true;
    }

    const std::string& Name() const override { return name_; }
    size_t WordCount() const override { return entries_.size(); }

    bool Lookup(const std::string& word, std::string& head, std::string& text) override {
        size_t i = LowerBound(word);
        for (; i < entries_.size() && CaseCmp(Word(i), word.c_str()) == 0; ++i) {
            // prefer an exact-case match among equal-ignoring-case headwords
            if (std::strcmp(Word(i), word.c_str()) == 0 || i + 1 >= entries_.size() || CaseCmp(Word(i + 1), word.c_str()) != 0) {
                head = Word(i);
                return Definition(i, text);
            }
        }
        return false;
    }

    std::vector<std::string> Prefix(const std::string& prefix, size_t max) override {
        std::vector<std::string> out;
        for (size_t i = LowerBound(prefix); i < entries_.size() && out.size() < max && StartsCase(Word(i), prefix); ++i)
            if (out.empty() || CaseCmp(out.back().c_str(), Word(i)) != 0) out.push_back(Word(i));
        return out;
    }

private:
    const char* Word(size_t i) const { return idx_.c_str() + entries_[i]; }

    size_t LowerBound(const std::string& w) const {
        size_t lo = 0, hi = entries_.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (CaseCmp(Word(mid), w.c_str()) < 0) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    bool Definition(size_t i, std::string& text) {
        const uint8_t* p = (const uint8_t*)Word(i) + std::strlen(Word(i)) + 1;
        uint64_t off = 0;
        int n = off64_ ? 8 : 4;
        for (int k = 0; k < n; ++k) off = (off << 8) | p[k];
        uint32_t size = ((uint32_t)p[n] << 24) | ((uint32_t)p[n + 1] << 16) | ((uint32_t)p[n + 2] << 8) | p[n + 3];
        if (size > 4u * 1024 * 1024) return false;
        std::string raw;
        if (!data_.Read(off, size, raw)) return false;
        text.clear();
        auto add = [&](const std::string& f, char type) {
            std::string t = PlainText(f, type);
            if (t.empty()) return;
            if (!text.empty()) text += "\n";
            text += type == 't' ? "/" + t + "/" : t;
        };
        size_t q = 0;
        if (!types_.empty()) {
            for (size_t k = 0; k < types_.size() && q <= raw.size(); ++k) {
                char ty = types_[k];
                const bool last = k + 1 == types_.size();
                if (std::islower((unsigned char)ty)) {
                    size_t z = last ? raw.size() : raw.find('\0', q);
                    if (z == std::string::npos) z = raw.size();
                    add(raw.substr(q, z - q), ty);
                    q = z + 1;
                } else {   // binary field (resources): skip
                    if (last) break;
                    if (q + 4 > raw.size()) break;
                    uint32_t len = ((uint8_t)raw[q] << 24) | ((uint8_t)raw[q + 1] << 16) | ((uint8_t)raw[q + 2] << 8) | (uint8_t)raw[q + 3];
                    q += 4 + len;
                }
            }
        } else {
            while (q < raw.size()) {
                char ty = raw[q++];
                if (std::islower((unsigned char)ty)) {
                    size_t z = raw.find('\0', q);
                    if (z == std::string::npos) z = raw.size();
                    add(raw.substr(q, z - q), ty);
                    q = z + 1;
                } else {
                    if (q + 4 > raw.size()) break;
                    uint32_t len = ((uint8_t)raw[q] << 24) | ((uint8_t)raw[q + 1] << 16) | ((uint8_t)raw[q + 2] << 8) | (uint8_t)raw[q + 3];
                    q += 4 + len;
                }
            }
        }
        return !text.empty();
    }

    static bool Gunzip(const std::string& in, std::string& out) {
        z_stream z{};
        if (inflateInit2(&z, 16 + 15) != Z_OK) return false;
        z.next_in = (Bytef*)in.data();
        z.avail_in = (uInt)in.size();
        char buf[65536];
        int r;
        do {
            z.next_out = (Bytef*)buf;
            z.avail_out = sizeof buf;
            r = inflate(&z, Z_NO_FLUSH);
            out.append(buf, sizeof buf - z.avail_out);
        } while (r == Z_OK);
        inflateEnd(&z);
        return r == Z_STREAM_END;
    }

    std::string name_, types_, idx_;
    bool off64_ = false;
    std::vector<uint32_t> entries_;
    DictData data_;
};

class TsvDict : public Dictionary {
public:
    bool Open(const std::string& path, std::string& err) {
        std::string all;
        if (!platform::ReadText(path, all)) { err = "không đọc được tệp"; return false; }
        std::string file = path.substr(path.rfind('/') + 1);
        name_ = file.substr(0, file.rfind('.'));
        size_t p = 0;
        while (p < all.size()) {
            size_t e = all.find('\n', p);
            std::string line = all.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? all.size() : e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string def = line.substr(tab + 1);
            for (size_t k; (k = def.find("\\n")) != std::string::npos;) def.replace(k, 2, "\n");
            for (auto& c : def) if (c == '\t') c = '\n';
            rows_.push_back({Trim(line.substr(0, tab)), def});
        }
        std::stable_sort(rows_.begin(), rows_.end(), [](auto& a, auto& b) { return CaseCmp(a.first.c_str(), b.first.c_str()) < 0; });
        if (rows_.empty()) { err = "không có dòng \"từ<TAB>nghĩa\" nào"; return false; }
        return true;
    }
    const std::string& Name() const override { return name_; }
    size_t WordCount() const override { return rows_.size(); }
    bool Lookup(const std::string& word, std::string& head, std::string& text) override {
        auto it = std::lower_bound(rows_.begin(), rows_.end(), word, [](auto& r, const std::string& w) { return CaseCmp(r.first.c_str(), w.c_str()) < 0; });
        if (it == rows_.end() || CaseCmp(it->first.c_str(), word.c_str()) != 0) return false;
        head = it->first;
        text = it->second;
        return true;
    }
    std::vector<std::string> Prefix(const std::string& prefix, size_t max) override {
        std::vector<std::string> out;
        auto it = std::lower_bound(rows_.begin(), rows_.end(), prefix, [](auto& r, const std::string& w) { return CaseCmp(r.first.c_str(), w.c_str()) < 0; });
        for (; it != rows_.end() && out.size() < max && StartsCase(it->first.c_str(), prefix); ++it) out.push_back(it->first);
        return out;
    }

private:
    std::string name_;
    std::vector<std::pair<std::string, std::string>> rows_;
};

std::vector<std::unique_ptr<Dictionary>> g_dicts;
std::vector<Dictionary*> g_ptrs;
bool g_loaded = false;

// English inflections, most specific first: (suffix, replacement).
const std::pair<const char*, const char*> kRules[] = {
    {"ies", "y"}, {"ied", "y"}, {"ier", "y"}, {"iest", "y"}, {"ily", "y"}, {"ves", "f"}, {"sses", "ss"},
    {"es", ""}, {"s", ""}, {"ed", ""}, {"ed", "e"}, {"d", ""}, {"ing", ""}, {"ing", "e"}, {"er", ""}, {"est", ""},
    {"ly", ""}, {"'s", ""}, {"s'", "s"},
};

}  // namespace

std::string PlainText(const std::string& data, char type) {
    if (type == 'm' || type == 'l' || type == 't' || type == 'y') return Trim(data);
    // h (HTML), g (Pango), x (XDXF): drop tags, line breaks for <br>, </p>, </div>, </li>, <k>…
    std::string out;
    out.reserve(data.size());
    for (size_t i = 0; i < data.size();) {
        if (data[i] == '<') {
            size_t e = data.find('>', i);
            if (e == std::string::npos) break;
            std::string tag = Lower(data.substr(i + 1, e - i - 1));
            if (tag.rfind("br", 0) == 0 || tag == "/p" || tag == "/div" || tag == "/li" || tag == "/k" || tag == "/tr" || tag.rfind("li", 0) == 0)
                if (!out.empty() && out.back() != '\n') out += '\n';
            i = e + 1;
        } else {
            out += data[i++];
        }
    }
    out = drive::DecodeEntities(out);
    std::string clean;   // collapse blank lines
    for (char c : out) {
        if (c == '\r') continue;
        if (c == '\n' && (clean.empty() || (clean.size() >= 2 && clean[clean.size() - 1] == '\n' && clean[clean.size() - 2] == '\n'))) continue;
        clean += c;
    }
    return Trim(clean);
}

std::unique_ptr<Dictionary> Open(const std::string& path, std::string& err) {
    std::string l = Lower(path);
    if (l.size() > 4 && l.compare(l.size() - 4, 4, ".ifo") == 0) {
        auto d = std::make_unique<StarDict>();
        if (d->Open(path, err)) return d;
        return nullptr;
    }
    if (l.size() > 4 && (l.compare(l.size() - 4, 4, ".tsv") == 0 || l.compare(l.size() - 4, 4, ".txt") == 0)) {
        auto d = std::make_unique<TsvDict>();
        if (d->Open(path, err)) return d;
        return nullptr;
    }
    err = "định dạng không hỗ trợ";
    return nullptr;
}

std::vector<Dictionary*>& All() {
    if (!g_loaded) {
        g_loaded = true;
        g_dicts.clear();
        g_ptrs.clear();
        const std::string dir = platform::DictDir();
        std::vector<std::string> files = platform::ListFiles(dir, {".ifo", ".tsv"});
        for (const auto& e : platform::ListDir(dir))   // one folder per dictionary is common too
            if (e.dir) for (const auto& f : platform::ListFiles(dir + "/" + e.name, {".ifo", ".tsv"})) files.push_back(e.name + "/" + f);
        for (const auto& f : files) {
            std::string err;
            auto d = Open(dir + "/" + f, err);
            if (d) g_dicts.push_back(std::move(d));
            else platform::Log("[dict] %s: %s", f.c_str(), err.c_str());
        }
        for (auto& d : g_dicts) g_ptrs.push_back(d.get());
    }
    return g_ptrs;
}

void Reload() { g_loaded = false; }

std::string CleanWord(const std::string& raw) {
    // Strip ASCII punctuation and common Unicode quotes/dashes around the word.
    static const char* uni[] = {"“", "”", "‘", "’", "…", "—", "–", "«", "»"};
    std::string w = raw;
    bool changed = true;
    while (changed && !w.empty()) {
        changed = false;
        while (!w.empty() && std::ispunct((unsigned char)w.front()) && w.front() != '\'') { w.erase(0, 1); changed = true; }
        while (!w.empty() && std::ispunct((unsigned char)w.back())) { w.pop_back(); changed = true; }
        for (const char* u : uni) {
            size_t n = std::strlen(u);
            if (w.size() >= n && w.compare(0, n, u) == 0) { w.erase(0, n); changed = true; }
            if (w.size() >= n && w.compare(w.size() - n, n, u) == 0) { w.erase(w.size() - n); changed = true; }
        }
    }
    if (w.size() >= 2 && w.compare(w.size() - 2, 2, "'s") == 0) w.erase(w.size() - 2);
    return w;
}

std::vector<Entry> LookupAll(const std::string& raw) {
    std::vector<Entry> out;
    std::string w = CleanWord(raw);
    if (w.empty()) return out;
    for (Dictionary* d : All()) {
        Entry e;
        e.dict = d->Name();
        if (d->Lookup(w, e.word, e.text)) { out.push_back(e); continue; }
        std::string lw = Lower(w);
        bool found = false;
        for (const auto& r : kRules) {
            size_t n = std::strlen(r.first);
            if (lw.size() <= n + 1 || lw.compare(lw.size() - n, n, r.first) != 0) continue;
            std::string base = lw.substr(0, lw.size() - n) + r.second;
            // doubled consonant: stopped → stop, running → run
            std::vector<std::string> tries = {base};
            if (std::strlen(r.second) == 0 && base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) tries.push_back(base.substr(0, base.size() - 1));
            for (const auto& t : tries)
                if (d->Lookup(t, e.word, e.text)) { e.via = w + " → " + e.word; found = true; break; }
            if (found) break;
        }
        if (found) out.push_back(e);
    }
    return out;
}

std::vector<std::string> Suggest(const std::string& prefix, size_t max) {
    std::vector<std::string> out;
    std::string p = CleanWord(prefix);
    if (p.empty()) return out;
    for (Dictionary* d : All()) {
        for (auto& w : d->Prefix(p, max)) {
            bool dup = false;
            for (auto& o : out) if (CaseCmp(o.c_str(), w.c_str()) == 0) dup = true;
            if (!dup) out.push_back(w);
        }
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return CaseCmp(a.c_str(), b.c_str()) < 0; });
    if (out.size() > max) out.resize(max);
    return out;
}

}  // namespace dict
