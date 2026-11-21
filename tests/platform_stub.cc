// Minimal platform layer for unit tests: real files under a temporary "SD card" directory.
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "platform.h"

std::string g_test_sd = "/tmp";

namespace platform {
namespace {
Config g_cfg;
}
const Config& GetConfig() { return g_cfg; }
bool FileExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
bool ReadText(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char b[8192];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    std::fclose(f);
    return true;
}
bool WriteText(const std::string& path, const std::string& d) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    return std::fclose(f) == 0 && ok;
}
bool RemoveFile(const std::string& p) { return std::remove(p.c_str()) == 0; }
uint64_t FileSize(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0; }
bool ReadRange(const std::string& path, uint64_t off, size_t len, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(len);
    bool ok = fseeko(f, (off_t)off, SEEK_SET) == 0;
    out.resize(ok ? std::fread(&out[0], 1, len, f) : 0);
    std::fclose(f);
    return ok;
}
std::vector<DirEntry> ListDir(const std::string& dir) {
    std::vector<DirEntry> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n.empty() || n[0] == '.') continue;
        struct stat st;
        if (::stat((dir + "/" + n).c_str(), &st) != 0) continue;
        out.push_back({n, S_ISDIR(st.st_mode), (uint64_t)st.st_size});
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.name < b.name; });
    return out;
}
std::vector<std::string> ListFiles(const std::string& dir, const std::vector<std::string>& exts) {
    std::vector<std::string> out;
    for (const auto& e : ListDir(dir)) {
        if (e.dir) continue;
        std::string l = e.name;
        for (auto& c : l) c = (char)std::tolower((unsigned char)c);
        for (const auto& x : exts) if (l.size() >= x.size() && l.compare(l.size() - x.size(), x.size(), x) == 0) { out.push_back(e.name); break; }
    }
    return out;
}
bool MakeDir(const std::string& p) { ::mkdir(p.c_str(), 0755); struct stat st; return ::stat(p.c_str(), &st) == 0; }
std::string SdRoot() { return g_test_sd; }
std::string StateDir() { return g_test_sd + "/.metalio"; }
std::string DictDir() { return g_test_sd + "/dict"; }
std::string VocabDir() { return g_test_sd + "/vocab"; }
std::string NotesDir() { return g_test_sd + "/notes"; }
std::string FontsDir() { return g_test_sd + "/fonts"; }
std::string MusicDir() { return g_test_sd + "/music"; }
std::string RecordingsDir() { return g_test_sd + "/recordings"; }
void ApplySettings(const settings::Values&) {}
void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}
}  // namespace platform
