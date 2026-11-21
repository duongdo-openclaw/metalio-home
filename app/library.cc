#include "library.h"

#include <algorithm>
#include <ctime>
#include <map>

#include "book.h"
#include "drive_folder.h"
#include "platform.h"
#include "reading_state.h"

namespace library {
namespace {

std::vector<std::string> g_books;
bool g_valid = false;
constexpr int kMaxDepth = 6;
constexpr size_t kMaxBooks = 3000;

void Walk(const std::string& dir, int depth) {
    if (depth > kMaxDepth || g_books.size() >= kMaxBooks) return;
    for (const auto& e : platform::ListDir(dir)) {
        if (e.dir) {
            if (depth == 0 && IsSystemFolder(e.name)) continue;
            Walk(dir + "/" + e.name, depth + 1);
        } else if (drive::IsReadable(e.name) && e.size > 0) {
            g_books.push_back(dir + "/" + e.name);
            if (g_books.size() >= kMaxBooks) return;
        }
    }
}

}  // namespace

bool IsSystemFolder(const std::string& n) {
    static const char* sys[] = {"music", "recordings", "fonts", "dict", "vocab", "notes", "System Volume Information", "LOST.DIR"};
    if (n.empty() || n[0] == '.') return true;
    for (const char* s : sys) if (n == s) return true;
    return false;
}

const std::vector<std::string>& Books() {
    if (!g_valid) {
        g_books.clear();
        Walk(platform::SdRoot(), 0);
        std::sort(g_books.begin(), g_books.end());
        g_valid = true;
    }
    return g_books;
}

void Invalidate() { g_valid = false; }

std::string Folder(const std::string& path) {
    std::string root = platform::SdRoot() + "/";
    std::string rel = path.compare(0, root.size(), root) == 0 ? path.substr(root.size()) : path;
    size_t slash = rel.rfind('/');
    return slash == std::string::npos ? "" : rel.substr(0, slash);
}

// Opening every EPUB on each visit is slow on the device; keep metadata per (path, size).
const Meta& LoadMeta(const std::string& path) {
    static std::map<std::string, std::pair<uint64_t, Meta>> cache;
    uint64_t size = platform::FileSize(path);
    auto it = cache.find(path);
    if (it != cache.end() && it->second.first == size) return it->second.second;
    Meta m;
    m.path = path;
    m.folder = Folder(path);
    book::Book b;
    std::string err, mime;
    m.ok = b.Open(path, err);
    std::string file = path.substr(path.rfind('/') + 1);
    m.title = m.ok && !b.Title().empty() ? b.Title() : file.substr(0, file.rfind('.'));
    m.author = m.ok ? b.Author() : "";
    m.chapters = m.ok ? b.ChapterCount() : 0;
    if (m.ok) b.Cover(m.cover, mime);
    cache[path] = {size, m};
    return cache[path].second;
}

int FinishedInYear(int year) {
    int n = 0;
    for (const auto& p : Books()) {
        reader::Progress pr;
        if (!reader::GetProgress(p, pr) || pr.percent < 99) continue;
        std::time_t t = (std::time_t)pr.updated;
        std::tm* tm = std::localtime(&t);
        if (tm && tm->tm_year + 1900 == year) ++n;
    }
    return n;
}

}  // namespace library
