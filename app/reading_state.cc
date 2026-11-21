#include "reading_state.h"

#include <cJSON.h>

#include <algorithm>
#include <ctime>

#include "platform.h"

namespace reader {
namespace {

std::string File() { return platform::StateDir() + "/reading.json"; }

cJSON* Load() {
    std::string s;
    cJSON* root = platform::ReadText(File(), s) ? cJSON_Parse(s.c_str()) : nullptr;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!cJSON_IsObject(cJSON_GetObjectItem(root, "books"))) {
        cJSON_DeleteItemFromObject(root, "books");
        cJSON_AddObjectToObject(root, "books");
    }
    return root;
}

void Store(cJSON* root) {
    char* s = cJSON_Print(root);
    if (s) platform::WriteText(File(), s);
    cJSON_free(s);
    cJSON_Delete(root);
}

int Int(const cJSON* o, const char* k, int def) {
    const cJSON* v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valueint : def;
}

}  // namespace

bool GetProgress(const std::string& path, Progress& out) {
    cJSON* root = Load();
    const cJSON* b = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "books"), path.c_str());
    bool ok = cJSON_IsObject(b);
    if (ok) {
        out.chapter = Int(b, "chapter", 0);
        out.para = Int(b, "para", 0);
        out.offset = (size_t)Int(b, "offset", 0);
        out.percent = Int(b, "percent", 0);
        const cJSON* u = cJSON_GetObjectItem(b, "updated");
        out.updated = cJSON_IsNumber(u) ? (long long)u->valuedouble : 0;
    }
    cJSON_Delete(root);
    return ok;
}

void SaveProgress(const std::string& path, const Progress& p) {
    cJSON* root = Load();
    cJSON* books = cJSON_GetObjectItem(root, "books");
    cJSON_DeleteItemFromObject(books, path.c_str());
    cJSON* b = cJSON_AddObjectToObject(books, path.c_str());
    cJSON_AddNumberToObject(b, "chapter", p.chapter);
    cJSON_AddNumberToObject(b, "para", p.para);
    cJSON_AddNumberToObject(b, "offset", (double)p.offset);
    cJSON_AddNumberToObject(b, "percent", p.percent);
    cJSON_AddNumberToObject(b, "updated", (double)std::time(nullptr));
    cJSON_DeleteItemFromObject(root, "last");
    cJSON_AddStringToObject(root, "last", path.c_str());
    Store(root);
}

std::string LastBook() {
    cJSON* root = Load();
    const cJSON* l = cJSON_GetObjectItem(root, "last");
    std::string out = cJSON_IsString(l) ? l->valuestring : "";
    cJSON_Delete(root);
    return platform::FileExists(out) ? out : "";
}

Settings GetSettings() {
    cJSON* root = Load();
    Settings s;
    const cJSON* o = cJSON_GetObjectItem(root, "settings");
    if (cJSON_IsObject(o)) {
        auto b = [o](const char* k, bool d) { const cJSON* v = cJSON_GetObjectItem(o, k); return cJSON_IsBool(v) ? cJSON_IsTrue(v) : d; };
        const cJSON* f = cJSON_GetObjectItem(o, "font");
        if (cJSON_IsString(f)) s.font = f->valuestring;
        s.font_px = std::clamp(Int(o, "font_px", s.font_px), 20, 72);
        s.bold = b("bold", s.bold);
        s.line_pct = std::clamp(Int(o, "line_pct", s.line_pct), 100, 220);
        s.letter = std::clamp(Int(o, "letter", s.letter), -1, 4);
        s.justify = b("justify", s.justify);
        s.margin_h = std::clamp(Int(o, "margin_h", s.margin_h), 12, 96);
        s.margin_v = std::clamp(Int(o, "margin_v", s.margin_v), 0, 80);
        s.indent = b("indent", s.indent);
        s.status = b("status", s.status);
        s.left_hand = b("left_hand", s.left_hand);
        s.auto_turn = std::clamp(Int(o, "auto_turn", s.auto_turn), 5, 300);
    }
    cJSON_Delete(root);
    return s;
}

void SaveSettings(const Settings& s) {
    cJSON* root = Load();
    cJSON_DeleteItemFromObject(root, "settings");
    cJSON* o = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddStringToObject(o, "font", s.font.c_str());
    cJSON_AddNumberToObject(o, "font_px", s.font_px);
    cJSON_AddBoolToObject(o, "bold", s.bold);
    cJSON_AddNumberToObject(o, "line_pct", s.line_pct);
    cJSON_AddNumberToObject(o, "letter", s.letter);
    cJSON_AddBoolToObject(o, "justify", s.justify);
    cJSON_AddNumberToObject(o, "margin_h", s.margin_h);
    cJSON_AddNumberToObject(o, "margin_v", s.margin_v);
    cJSON_AddBoolToObject(o, "indent", s.indent);
    cJSON_AddBoolToObject(o, "status", s.status);
    cJSON_AddBoolToObject(o, "left_hand", s.left_hand);
    cJSON_AddNumberToObject(o, "auto_turn", s.auto_turn);
    Store(root);
}

std::vector<Bookmark> GetBookmarks(const std::string& path) {
    std::vector<Bookmark> out;
    cJSON* root = Load();
    const cJSON* arr = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "bookmarks"), path.c_str());
    const cJSON* it = nullptr;
    cJSON_ArrayForEach(it, arr) {
        Bookmark b;
        b.chapter = Int(it, "chapter", 0);
        b.para = Int(it, "para", 0);
        b.offset = (size_t)Int(it, "offset", 0);
        const cJSON* sn = cJSON_GetObjectItem(it, "snippet");
        if (cJSON_IsString(sn)) b.snippet = sn->valuestring;
        const cJSON* c = cJSON_GetObjectItem(it, "created");
        b.created = cJSON_IsNumber(c) ? (long long)c->valuedouble : 0;
        out.push_back(b);
    }
    cJSON_Delete(root);
    return out;
}

void SaveBookmarks(const std::string& path, const std::vector<Bookmark>& marks) {
    cJSON* root = Load();
    cJSON* all = cJSON_GetObjectItem(root, "bookmarks");
    if (!cJSON_IsObject(all)) {
        cJSON_DeleteItemFromObject(root, "bookmarks");
        all = cJSON_AddObjectToObject(root, "bookmarks");
    }
    cJSON_DeleteItemFromObject(all, path.c_str());
    cJSON* arr = cJSON_AddArrayToObject(all, path.c_str());
    for (const auto& b : marks) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "chapter", b.chapter);
        cJSON_AddNumberToObject(o, "para", b.para);
        cJSON_AddNumberToObject(o, "offset", (double)b.offset);
        cJSON_AddStringToObject(o, "snippet", b.snippet.c_str());
        cJSON_AddNumberToObject(o, "created", (double)b.created);
        cJSON_AddItemToArray(arr, o);
    }
    Store(root);
}

namespace {
std::string DayKey(std::time_t t) {
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", std::localtime(&t));
    return b;
}
}  // namespace

void AddReading(int pages, int seconds) {
    if (pages <= 0 && seconds <= 0) return;
    cJSON* root = Load();
    cJSON* stats = cJSON_GetObjectItem(root, "stats");
    if (!cJSON_IsObject(stats)) {
        cJSON_DeleteItemFromObject(root, "stats");
        stats = cJSON_AddObjectToObject(root, "stats");
    }
    std::string key = DayKey(std::time(nullptr));
    cJSON* day = cJSON_GetObjectItem(stats, key.c_str());
    int p0 = Int(day, "pages", 0), s0 = Int(day, "seconds", 0);
    cJSON_DeleteItemFromObject(stats, key.c_str());
    day = cJSON_AddObjectToObject(stats, key.c_str());
    cJSON_AddNumberToObject(day, "pages", p0 + pages);
    cJSON_AddNumberToObject(day, "seconds", s0 + seconds);
    Store(root);
}

Stats GetStats() {
    Stats st;
    cJSON* root = Load();
    const cJSON* stats = cJSON_GetObjectItem(root, "stats");
    std::time_t now = std::time(nullptr);
    auto secs = [&](int days_ago, int* pages) {
        const cJSON* d = cJSON_GetObjectItem(stats, DayKey(now - (std::time_t)days_ago * 86400).c_str());
        if (pages) *pages = Int(d, "pages", 0);
        return Int(d, "seconds", 0);
    };
    st.today_seconds = secs(0, &st.today_pages);
    for (int i = 0; i < 7; ++i) {
        std::time_t t = now - (std::time_t)(6 - i) * 86400;
        st.week_seconds[i] = secs(6 - i, nullptr);
        st.week_day[i] = std::localtime(&t)->tm_wday;
    }
    int start = st.today_seconds > 0 ? 0 : 1;  // today not read yet: the streak is still alive
    for (int d = start; d < 365 && secs(d, nullptr) > 0; ++d) st.streak_days++;
    cJSON_Delete(root);
    return st;
}

}  // namespace reader
