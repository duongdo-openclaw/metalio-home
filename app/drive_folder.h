// Public ("anyone with the link") Google Drive folder listing without an API key.
// Port of Kreader's Drive.java: lists via drive.google.com/embeddedfolderview and
// downloads via drive.usercontent.google.com. Pure C++, no regex (keeps ESP32 flash/RAM small).
#pragma once
#include <string>
#include <vector>

namespace drive {

struct Ref {             // folder or file reference
    std::string id;
    std::string resource_key;  // empty when the link has none
    bool empty() const { return id.empty(); }
};

struct Item {
    Ref ref;
    std::string name;
    bool folder = false;
};

struct Folder {
    std::string title;
    std::vector<Item> items;
};

// Accepts a pasted folder URL (…/folders/<id>?resourcekey=…), "id=<id>", or a bare id.
Ref ParseLink(const std::string& link);

std::string ListUrl(const Ref& folder);
std::string DownloadUrl(const Ref& file);

// Parses the embeddedfolderview HTML. Returns false when it is not a public folder page.
bool ParseFolderHtml(const std::string& html, Folder& out);

// Book formats the Metalio reader opens (firmware: epub_document / txt_chapter).
bool IsReadable(const std::string& name);
// Audio the player opens (MP3, WAV) — downloaded to the music folder instead of books.
bool IsAudio(const std::string& name);

std::string DecodeEntities(const std::string& s);

// File name safe for FAT on the SD card.
std::string SafeFileName(const std::string& name);

}  // namespace drive
