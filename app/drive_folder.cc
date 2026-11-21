#include "drive_folder.h"

#include <cctype>
#include <cstdlib>

namespace drive {
namespace {

bool IdChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-'; }

std::string TakeId(const std::string& s, size_t at) {
    size_t e = at;
    while (e < s.size() && IdChar(s[e])) ++e;
    return s.substr(at, e - at);
}

std::string FindKey(const std::string& s) {
    size_t at = s.find("resourcekey=");
    return at == std::string::npos ? "" : TakeId(s, at + 12);
}

std::string Enc(const std::string& s) {  // ids/keys are [A-Za-z0-9_-] already; be safe anyway
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (IdChar((char)c) || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

void AppendUtf8(std::string& o, unsigned long cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
        o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
    }
}

// Text between `open_marker`'s closing '>' and the next "</div>".
bool InnerText(const std::string& h, size_t from, size_t limit, const char* marker, std::string& out) {
    size_t m = h.find(marker, from);
    if (m == std::string::npos || m >= limit) return false;
    size_t gt = h.find('>', m);
    if (gt == std::string::npos || gt >= limit) return false;
    size_t end = h.find("</div>", gt);
    if (end == std::string::npos || end > limit) return false;
    out = h.substr(gt + 1, end - gt - 1);
    return true;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

}  // namespace

Ref ParseLink(const std::string& raw) {
    Ref r;
    std::string s = Trim(raw);
    r.resource_key = FindKey(s);
    size_t at = s.find("/folders/");
    if (at != std::string::npos) r.id = TakeId(s, at + 9);
    else if ((at = s.find("id=")) != std::string::npos) r.id = TakeId(s, at + 3);
    else r.id = TakeId(s, 0);
    if (r.id.size() < 10) r = Ref{};
    return r;
}

std::string ListUrl(const Ref& f) {
    std::string u = "https://drive.google.com/embeddedfolderview?id=" + Enc(f.id);
    if (!f.resource_key.empty()) u += "&resourcekey=" + Enc(f.resource_key);
    return u;
}

std::string DownloadUrl(const Ref& f) {
    std::string u = "https://drive.usercontent.google.com/download?id=" + Enc(f.id) +
                    "&export=download&confirm=t";
    if (!f.resource_key.empty()) u += "&resourcekey=" + Enc(f.resource_key);
    return u;
}

std::string DecodeEntities(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { o += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { o += s[i]; continue; }
        std::string e = s.substr(i + 1, semi - i - 1);
        if (e == "amp") o += '&';
        else if (e == "lt") o += '<';
        else if (e == "gt") o += '>';
        else if (e == "quot") o += '"';
        else if (e == "apos") o += '\'';
        else if (e == "nbsp") o += ' ';
        else if (e.size() > 1 && e[0] == '#') {
            unsigned long cp = (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16)
                                                           : std::strtoul(e.c_str() + 1, nullptr, 10);
            AppendUtf8(o, cp);
        } else { o += s.substr(i, semi - i + 1); }
        i = semi;
    }
    return o;
}

bool ParseFolderHtml(const std::string& h, Folder& out) {
    out = Folder{};
    if (h.find("flip-entries") == std::string::npos) return false;
    size_t t = h.find("<title>");
    if (t != std::string::npos) {
        size_t te = h.find("</title>", t);
        if (te != std::string::npos) out.title = Trim(DecodeEntities(h.substr(t + 7, te - t - 7)));
    }
    static const std::string kEntry = "class=\"flip-entry\" id=\"entry-";
    size_t pos = h.find(kEntry);
    while (pos != std::string::npos) {
        size_t next = h.find(kEntry, pos + kEntry.size());
        size_t limit = next == std::string::npos ? h.size() : next;
        Item it;
        it.ref.id = TakeId(h, pos + kEntry.size());
        size_t a = h.find("<a href=\"", pos);
        if (a != std::string::npos && a < limit) {
            size_t ae = h.find('"', a + 9);
            std::string href = DecodeEntities(h.substr(a + 9, ae - a - 9));
            it.folder = href.find("/folders/") != std::string::npos;
            it.ref.resource_key = FindKey(href);
        }
        std::string title;
        if (!it.ref.id.empty() && InnerText(h, pos, limit, "class=\"flip-entry-title\"", title)) {
            it.name = Trim(DecodeEntities(title));
            out.items.push_back(it);
        }
        pos = next;
    }
    return true;
}

static std::string Ext(const std::string& name) {
    size_t dot = name.rfind('.');
    std::string ext;
    if (dot == std::string::npos) return ext;
    for (size_t i = dot + 1; i < name.size(); ++i) ext += (char)std::tolower((unsigned char)name[i]);
    return ext;
}

bool IsReadable(const std::string& name) {
    std::string e = Ext(name);
    return e == "epub" || e == "txt";
}

bool IsAudio(const std::string& name) {
    std::string e = Ext(name);
    return e == "mp3" || e == "wav";
}

std::string SafeFileName(const std::string& name) {
    std::string o;
    for (unsigned char c : name) {
        if (c < 0x20 || std::string("\\/:*?\"<>|").find((char)c) != std::string::npos) o += '_';
        else o += (char)c;
    }
    o = Trim(o);
    while (!o.empty() && (o.back() == '.' || o.back() == ' ')) o.pop_back();
    if (o.empty()) o = "book";
    if (o.size() > 200) {  // keep extension
        size_t dot = o.rfind('.');
        std::string ext = dot != std::string::npos && o.size() - dot <= 6 ? o.substr(dot) : "";
        o = o.substr(0, 200 - ext.size()) + ext;
    }
    return o;
}

}  // namespace drive
