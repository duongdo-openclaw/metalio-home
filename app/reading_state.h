// Reading progress + reader settings, stored as JSON on the SD card (<sd>/.metalio/reading.json).
#pragma once
#include <string>
#include <vector>

namespace reader {

struct Progress {
    int chapter = 0;
    int para = 0;
    size_t offset = 0;
    int percent = 0;       // whole book, for the library badge
    long long updated = 0; // unix seconds
};

// Display options of the reader (Kreader's "Phông chữ / Đoạn văn / Lề" sheets).
struct Settings {
    std::string font = "serif";  // "serif", "sans", or "file:<name>.ttf" from <sd>/fonts
    int font_px = 38;            // stock reader default is MiSans 40 px (~9.7 pt at 297 ppi)
    bool bold = false;
    int line_pct = 150;          // line height, % of the font size (100..220)
    int letter = 0;              // extra px between letters (-1..4)
    bool justify = true;         // căn đều hai bên
    int margin_h = 40;           // left/right margin px
    int margin_v = 24;           // extra top/bottom margin px
    bool indent = true;          // true: first-line indent; false: blank space between paragraphs
    bool status = true;          // header + footer lines
    bool left_hand = false;      // tap the LEFT side for the next page
    int auto_turn = 30;          // seconds per page for "tự lật trang"
};

struct Bookmark {
    int chapter = 0, para = 0;
    size_t offset = 0;
    std::string snippet;         // first words of the page
    long long created = 0;
};
std::vector<Bookmark> GetBookmarks(const std::string& book_path);
void SaveBookmarks(const std::string& book_path, const std::vector<Bookmark>& marks);

bool GetProgress(const std::string& book_path, Progress& out);
void SaveProgress(const std::string& book_path, const Progress& p);
std::string LastBook();   // most recently read book path, "" if none
Settings GetSettings();

// Reading statistics (per local day), fed by the reader on every page turn.
struct Stats {
    int today_seconds = 0, today_pages = 0;
    int streak_days = 0;          // consecutive days with reading, ending today or yesterday
    int week_seconds[7] = {};     // [0] = 6 days ago … [6] = today
    int week_day[7] = {};         // weekday of each bar, 0 = Sunday
};
void AddReading(int pages, int seconds);
Stats GetStats();
void SaveSettings(const Settings& s);

}  // namespace reader
