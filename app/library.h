// The library lives on the SD card: every EPUB/TXT anywhere on the card (any folder layout the
// user copies over USB or Wi-Fi), except the system folders. Metadata is cached per (path, size).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace library {

struct Meta {
    std::string path, title, author;
    std::string folder;               // relative to the card root, "" = root
    std::vector<uint8_t> cover;
    int chapters = 0;
    bool ok = false;
};

const Meta& LoadMeta(const std::string& path);
const std::vector<std::string>& Books();   // full paths, sorted by path; cached until Invalidate()
void Invalidate();                          // after downloads, uploads, deletes, format
std::string Folder(const std::string& path);
int FinishedInYear(int year);               // books at >= 99 % last read in that year
bool IsSystemFolder(const std::string& name);

}  // namespace library
