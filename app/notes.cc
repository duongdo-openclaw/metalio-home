#include "notes.h"

#include <cJSON.h>

#include <algorithm>
#include <ctime>

#include "drive_folder.h"   // SafeFileName
#include "platform.h"

namespace notes {
namespace {

std::vector<Note> g_notes;
bool g_loaded = false;
int g_next = 1;

std::string File() { return platform::StateDir() + "/notes.json"; }

std::string Str(const cJSON* o, const char* k) {
    const cJSON* v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
double Num(const cJSON* o, const char* k) {
    const cJSON* v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::string s;
    if (!platform::ReadText(File(), s)) return;
    cJSON* root = cJSON_Parse(s.c_str());
    const cJSON* arr = cJSON_GetObjectItem(root, "notes");
    const cJSON* it;
    cJSON_ArrayForEach(it, arr) {
        Note n;
        n.id = (int)Num(it, "id");
        n.book = Str(it, "book");
        n.book_title = Str(it, "book_title");
        n.chapter_title = Str(it, "chapter_title");
        n.chapter = (int)Num(it, "chapter");
        n.para = (int)Num(it, "para");
        n.offset = (size_t)Num(it, "offset");
        n.para_end = (int)Num(it, "para_end");
        n.offset_end = (size_t)Num(it, "offset_end");
        n.quote = Str(it, "quote");
        n.text = Str(it, "text");
        n.created = (long long)Num(it, "created");
        n.updated = (long long)Num(it, "updated");
        if (n.id <= 0) continue;
        g_next = std::max(g_next, n.id + 1);
        g_notes.push_back(n);
    }
    cJSON_Delete(root);
}

std::string Date(long long t) {
    std::time_t tt = (std::time_t)t;
    char b[32];
    std::strftime(b, sizeof b, "%d/%m/%Y %H:%M", std::localtime(&tt));
    return b;
}

void ExportBook(const std::string& book) {
    platform::MakeDir(platform::NotesDir());
    Note probe;
    probe.book = book;
    for (const auto& n : g_notes) if (n.book == book) { probe = n; break; }
    const std::string path = platform::NotesDir() + "/" + ExportName(probe);
    bool any = std::any_of(g_notes.begin(), g_notes.end(), [&](const Note& n) { return n.book == book; });
    if (!any) { platform::RemoveFile(path); return; }
    platform::WriteText(path, Markdown(book));
}

void Save(const std::string& touched_book) {
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "notes");
    for (const auto& n : g_notes) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", n.id);
        cJSON_AddStringToObject(o, "book", n.book.c_str());
        cJSON_AddStringToObject(o, "book_title", n.book_title.c_str());
        cJSON_AddStringToObject(o, "chapter_title", n.chapter_title.c_str());
        cJSON_AddNumberToObject(o, "chapter", n.chapter);
        cJSON_AddNumberToObject(o, "para", n.para);
        cJSON_AddNumberToObject(o, "offset", (double)n.offset);
        cJSON_AddNumberToObject(o, "para_end", n.para_end);
        cJSON_AddNumberToObject(o, "offset_end", (double)n.offset_end);
        cJSON_AddStringToObject(o, "quote", n.quote.c_str());
        cJSON_AddStringToObject(o, "text", n.text.c_str());
        cJSON_AddNumberToObject(o, "created", (double)n.created);
        cJSON_AddNumberToObject(o, "updated", (double)n.updated);
        cJSON_AddItemToArray(arr, o);
    }
    char* s = cJSON_Print(root);
    if (s) platform::WriteText(File(), s);
    cJSON_free(s);
    cJSON_Delete(root);
    ExportBook(touched_book);
}

}  // namespace

std::string ExportName(const Note& n) {
    if (n.book.empty()) return "ghi-chu-rieng.md";
    std::string file = n.book.substr(n.book.rfind('/') + 1);
    file = file.substr(0, file.rfind('.'));
    std::string safe = drive::SafeFileName(file);
    return (safe.empty() ? "sach" : safe) + ".md";
}

std::vector<Note> All() {
    Load();
    std::vector<Note> v = g_notes;
    std::sort(v.begin(), v.end(), [](const Note& a, const Note& b) { return a.updated != b.updated ? a.updated > b.updated : a.id > b.id; });
    return v;
}

std::vector<Note> ForBook(const std::string& book) {
    std::vector<Note> v;
    for (const auto& n : All()) if (n.book == book) v.push_back(n);
    std::sort(v.begin(), v.end(), [](const Note& a, const Note& b) {
        return a.chapter != b.chapter ? a.chapter < b.chapter : a.para != b.para ? a.para < b.para : a.offset < b.offset;
    });
    return v;
}

bool Get(int id, Note& out) {
    Load();
    for (const auto& n : g_notes) if (n.id == id) { out = n; return true; }
    return false;
}

int Add(Note n) {
    Load();
    n.id = g_next++;
    n.created = n.updated = (long long)std::time(nullptr);
    g_notes.push_back(n);
    Save(n.book);
    return n.id;
}

bool Update(const Note& n) {
    Load();
    for (auto& x : g_notes)
        if (x.id == n.id) {
            x = n;
            x.updated = (long long)std::time(nullptr);
            Save(x.book);
            return true;
        }
    return false;
}

bool Remove(int id) {
    Load();
    for (size_t i = 0; i < g_notes.size(); ++i)
        if (g_notes[i].id == id) {
            std::string book = g_notes[i].book;
            g_notes.erase(g_notes.begin() + (long)i);
            Save(book);
            return true;
        }
    return false;
}

std::string Markdown(const std::string& book) {
    std::vector<Note> v = ForBook(book);
    std::string title = book.empty() ? "Ghi chú riêng" : (v.empty() || v[0].book_title.empty() ? book : v[0].book_title);
    std::string md = "# " + title + "\n\n_" + std::to_string(v.size()) + " ghi chú · xuất từ máy đọc sách Metalio_\n";
    std::string chapter = "\x01";
    for (const auto& n : v) {
        if (!book.empty() && n.chapter_title != chapter) {
            chapter = n.chapter_title;
            md += "\n## " + (chapter.empty() ? "Chương " + std::to_string(n.chapter + 1) : chapter) + "\n";
        }
        md += "\n";
        if (!n.quote.empty()) md += "> " + n.quote + "\n\n";
        if (!n.text.empty()) md += n.text + "\n\n";
        md += "<sub>" + Date(n.updated) + "</sub>\n";
    }
    return md;
}

}  // namespace notes
