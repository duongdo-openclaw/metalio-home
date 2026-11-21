// "Từ điển": offline lookup in the StarDict/TSV dictionaries of <sd>/dict. Search with the
// on-screen keyboard (English by default, Telex for Vietnamese), suggestions by prefix, recent
// lookups, and "+ Từ vựng" to study a word later in "Học từ vựng".
#include <algorithm>
#include <cstdio>
#include <deque>

#include "dictionary.h"
#include "icons.h"
#include "platform.h"
#include "screens.h"
#include "ui_common.h"
#include "vocab.h"

namespace screens {
namespace {

const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);
std::deque<std::string> g_recent;   // newest first, kept for the session

std::string FirstLine(const std::string& s) {
    size_t n = s.find('\n');
    return n == std::string::npos ? s : s.substr(0, n);
}

void Remember(const std::string& w) {
    g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), w), g_recent.end());
    g_recent.push_front(w);
    if (g_recent.size() > 12) g_recent.pop_back();
}

lv_obj_t* Column(lv_obj_t* parent) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

void Search(const std::string& initial);

}  // namespace

bool FillLookup(lv_obj_t* box, const std::string& raw, const std::string& example, bool compact) {
    std::string word = dict::CleanWord(raw);
    auto entries = dict::LookupAll(word);
    if (dict::All().empty()) {
        lv_obj_t* l = ui::Label(box, "Chưa có từ điển. Chép từ điển StarDict (.ifo .idx .dict/.dict.dz) hoặc tệp .tsv vào thư mục dict/ trên thẻ nhớ, hoặc gửi qua Chuyển file.", ui::F().body);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        return false;
    }
    lv_obj_t* head = lv_obj_create(box);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* w = ui::Label(head, entries.empty() ? word : entries[0].word, ui::F().reader(compact ? 40 : 52, true));
    lv_obj_align(w, LV_ALIGN_TOP_LEFT, 0, 0);
    if (entries.empty()) {
        lv_obj_t* l = ui::Label(box, "Không tìm thấy \"" + word + "\" trong " + std::to_string(dict::All().size()) + " từ điển.", ui::F().body);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        auto sug = dict::Suggest(word.substr(0, std::max<size_t>(2, word.size() / 2)), 6);
        if (!sug.empty()) {
            std::string all = "Gần giống: ";
            for (size_t i = 0; i < sug.size(); ++i) all += (i ? ", " : "") + sug[i];
            lv_obj_t* s = ui::Label(box, all, ui::F().small);
            lv_obj_set_style_text_color(s, kMuted, 0);
            lv_obj_set_width(s, LV_PCT(100));
            lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
        }
        return false;
    }
    Remember(entries[0].word);
    for (const auto& e : entries) {
        lv_obj_t* src = ui::Label(box, std::string(icon::DICT) + "  " + e.dict + (e.via.empty() ? "" : "   ·   " + e.via), ui::F().small);
        lv_obj_set_style_text_color(src, kMuted, 0);
        lv_obj_set_width(src, LV_PCT(100));
        lv_label_set_long_mode(src, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_t* t = ui::Label(box, e.text, compact ? ui::F().body : ui::F().reader(32, false));
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_line_space(t, 6, 0);
        if (compact) break;   // the reader sheet shows the first dictionary only
    }
    // Phonetic = the first /…/ in the definition, meaning = the rest of the first line.
    std::string def = FirstLine(entries[0].text), phon;
    if (def.size() > 2 && def[0] == '/') {
        size_t e = def.find('/', 1);
        if (e != std::string::npos) { phon = def.substr(0, e + 1); def = def.substr(e + 1); }
    }
    while (!def.empty() && def[0] == ' ') def.erase(0, 1);
    std::string head_word = entries[0].word;
    lv_obj_t* add = ui::Button(head, "", [head_word, phon, def, example] {
        if (vocab::AddWord(head_word, phon, def, example)) ui::Toast("Đã thêm \"" + head_word + "\" vào Từ của tôi.");
        else ui::Toast("\"" + head_word + "\" đã có trong Từ của tôi.");
    }, LV_SIZE_CONTENT, 60);
    lv_obj_set_style_pad_hor(add, 16, 0);
    lv_obj_set_style_radius(add, 30, 0);
    lv_obj_center(ui::Label(add, std::string(icon::PLUS) + " Từ vựng", ui::F().body_bold));
    if (vocab::HasWord(head_word)) ui::Invert(add, true);
    lv_obj_align(add, LV_ALIGN_TOP_RIGHT, 0, 4);
    return true;
}

namespace {

void ShowWord(const std::string& word);

void Home() {
    ui::Page p = ui::NewPage("TỪ ĐIỂN", [] { OpenApps(); });
    size_t words = 0;
    for (auto* d : dict::All()) words += d->WordCount();
    lv_label_set_text_fmt(p.header_status, "%d từ điển · %u mục từ · ngoại tuyến", (int)dict::All().size(), (unsigned)words);
    lv_obj_set_style_pad_row(p.body, 14, 0);
    lv_obj_t* sb = ui::Button(p.body, "", [] { Search(""); }, LV_PCT(100), 84);
    lv_obj_set_style_radius(sb, 42, 0);
    lv_obj_set_style_border_width(sb, 3, 0);
    lv_obj_t* sl = ui::Label(sb, std::string(icon::SEARCH) + "   Tra một từ…", ui::F().body);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 12, 0);

    if (!g_recent.empty()) {
        lv_obj_t* h = ui::Label(p.body, "TRA GẦN ĐÂY", ui::F().small);
        lv_obj_set_style_text_letter_space(h, 3, 0);
        lv_obj_set_style_text_color(h, kMuted, 0);
        lv_obj_t* chips = lv_obj_create(p.body);
        lv_obj_set_size(chips, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(chips, 0, 0);
        lv_obj_set_style_pad_all(chips, 0, 0);
        lv_obj_set_style_pad_gap(chips, 10, 0);
        lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_remove_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
        for (const auto& w : g_recent) {
            lv_obj_t* b = ui::Button(chips, w, [w] { ShowWord(w); }, LV_SIZE_CONTENT, 64);
            lv_obj_set_style_radius(b, 32, 0);
            lv_obj_set_style_pad_hor(b, 18, 0);
            lv_obj_set_style_border_width(b, 1, 0);
        }
    }
    lv_obj_t* h = ui::Label(p.body, "TỪ ĐIỂN TRÊN THẺ NHỚ", ui::F().small);
    lv_obj_set_style_text_letter_space(h, 3, 0);
    lv_obj_set_style_text_color(h, kMuted, 0);
    for (auto* d : dict::All()) {
        lv_obj_t* row = lv_obj_create(p.body);
        lv_obj_set_size(row, LV_PCT(100), 80);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, kHair, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(ui::IconLabel(row, icon::DICT), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* n = ui::Label(row, d->Name(), ui::F().body_bold);
        lv_obj_set_width(n, 420);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 60, 0);
        lv_obj_align(ui::Label(row, std::to_string(d->WordCount()) + " từ", ui::F().small), LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_t* hint = ui::Label(p.body, "Thêm từ điển: chép bộ StarDict (.ifo + .idx + .dict hoặc .dict.dz) hoặc tệp .tsv \"từ<TAB>nghĩa\" vào thư mục dict/ trên thẻ, hoặc gửi qua Ứng dụng › Chuyển file. Khi đọc sách: giữ ngón tay lên một từ để tra.", ui::F().small);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    ui::Button(p.body, "Nạp lại từ điển", [] { dict::Reload(); Home(); }, LV_PCT(100), 72);
    ui::Show(p.screen);
}

void ShowWord(const std::string& word) {
    ui::Page p = ui::NewPage("TỪ ĐIỂN", [] { Home(); });
    lv_label_set_text(p.header_status, "Bấm phím lật trang để xem tiếp định nghĩa dài");
    lv_obj_t* tools = lv_obj_create(p.body);
    lv_obj_set_size(tools, LV_PCT(100), 72);
    lv_obj_set_style_border_width(tools, 0, 0);
    lv_obj_set_style_pad_all(tools, 0, 0);
    lv_obj_remove_flag(tools, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* sb = ui::Button(tools, "", [word] { Search(word); }, LV_PCT(100), 70);
    lv_obj_set_style_radius(sb, 35, 0);
    lv_obj_align(ui::Label(sb, std::string(icon::SEARCH) + "   " + word, ui::F().body), LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t* box = Column(p.body);
    lv_obj_set_style_pad_row(box, 12, 0);
    FillLookup(box, word, "", false);
    lv_obj_t* body = p.body;
    ui::SetPageKeyHandler([body](int dir) { lv_obj_scroll_by_bounded(body, 0, -dir * (lv_obj_get_height(body) - 120), LV_ANIM_OFF); });
    ui::Show(p.screen);
}

void Search(const std::string& initial) {
    ui::PromptOptions o;
    o.title = "Tra từ";
    o.hint = "Gõ từ cần tra. Bật Tiếng Việt (Telex) để tra từ tiếng Việt.";
    o.initial = initial;
    o.placeholder = "Ví dụ: patience";
    o.vietnamese = false;
    o.ok = "Tra";
    ui::TextPrompt(o, [](const std::string& w) { if (!dict::CleanWord(w).empty()) ShowWord(w); });
}

}  // namespace

void OpenDictionary(const std::string& word) {
    if (word.empty()) Home();
    else ShowWord(word);
}

}  // namespace screens
