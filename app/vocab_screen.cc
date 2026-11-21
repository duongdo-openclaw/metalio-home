// "Học từ vựng": decks on the SD card studied with spaced repetition.
//   Home:      today's reviews + new words, streak, decks (Từ của tôi first), new-words-per-day.
//   Flashcard: word → tap / page key to show the meaning → Quên · Khó · Nhớ · Dễ (with intervals).
//   Quiz:      word + four meanings from the same deck.
//   List:      every word of a deck with its next review.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>

#include "icons.h"
#include "screens.h"
#include "ui_common.h"
#include "vocab.h"

namespace screens {
namespace {

const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);
int g_new_per_day = 10;
std::string g_deck;          // "" = all decks

struct Study {
    std::vector<vocab::Card> cards;
    size_t i = 0;
    bool shown = false;
    int done = 0, again = 0;
    bool quiz = false;
    std::vector<std::string> choices;
    int answer = -1, picked = -1;
    lv_obj_t* body = nullptr;
    lv_obj_t* status = nullptr;
    std::shared_ptr<bool> alive;
};

void Home();
void RenderCard(Study* s);

std::string Days(int d) {
    if (d <= 0) return "hôm nay";
    if (d < 30) return std::to_string(d) + " ngày";
    if (d < 365) return std::to_string(d / 30) + " tháng";
    return std::to_string(d / 365) + " năm";
}

lv_obj_t* Plain(lv_obj_t* parent, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

lv_obj_t* Wrap(lv_obj_t* parent, const std::string& t, const lv_font_t* f, lv_color_t c = lv_color_black()) {
    lv_obj_t* l = ui::Label(parent, t, f);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

void Finish(Study* s) {
    lv_obj_clean(s->body);
    lv_obj_t* box = Plain(s->body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 18, 0);
    lv_obj_set_style_pad_top(box, 120, 0);
    lv_obj_t* ic = ui::IconLabel(box, icon::TROPHY, ui::IconFont(96));
    (void)ic;
    Wrap(box, "Xong buổi học!", ui::F().serif_title);
    char b[128];
    std::snprintf(b, sizeof b, "%d lượt ôn hôm nay · chuỗi %d ngày", vocab::StudiedToday(), vocab::Streak());
    Wrap(box, b, ui::F().body, kMuted);
    ui::Button(box, "Về Học từ vựng", [] { Home(); }, 400, 80);
    lv_label_set_text(s->status, "Hoàn thành");
    ui::SetPageKeyHandler(nullptr);
}

void Grade(Study* s, vocab::Grade g) {
    vocab::Card& c = s->cards[s->i];
    vocab::Review(c, g);
    s->done++;
    if (g == vocab::Grade::Again) { s->again++; s->cards.push_back(c); }   // see it again this session
    s->i++;
    s->shown = false;
    s->picked = -1;
    RenderCard(s);
}

void MakeChoices(Study* s) {
    s->choices.clear();
    const vocab::Card& c = s->cards[s->i];
    std::vector<std::string> pool;
    for (const auto& x : vocab::Cards(c.deck)) if (x.word != c.word && !x.meaning.empty()) pool.push_back(x.meaning);
    std::mt19937 rng((unsigned)(s->i * 7919 + s->done + vocab::Today()));
    std::shuffle(pool.begin(), pool.end(), rng);
    pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
    pool.resize(std::min<size_t>(3, pool.size()));
    pool.push_back(c.meaning);
    std::shuffle(pool.begin(), pool.end(), rng);
    s->choices = pool;
    s->answer = (int)(std::find(pool.begin(), pool.end(), c.meaning) - pool.begin());
}

void RenderCard(Study* s) {
    if (s->i >= s->cards.size()) { Finish(s); return; }
    lv_obj_clean(s->body);
    const vocab::Card& c = s->cards[s->i];
    lv_label_set_text_fmt(s->status, "Thẻ %d / %d · %s", (int)s->i + 1, (int)s->cards.size(),
                          c.learned() ? "ôn lại" : "từ mới");
    // progress rule
    lv_obj_t* track = Plain(s->body, LV_PCT(100), 8);   // 1-bit screen: outlined track, black fill
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_border_color(track, lv_color_black(), 0);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_t* fill = Plain(track, std::max(8, (ui::kW - 2 * ui::kPad) * (int)s->i / (int)s->cards.size()), 8);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_black(), 0);

    // The card
    lv_obj_t* card = ui::Button(s->body, "", [s] { if (!s->quiz && !s->shown) { s->shown = true; RenderCard(s); } }, LV_PCT(100), s->quiz ? 380 : 620);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_pad_all(card, 28, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 14, 0);
    lv_obj_t* deck = Wrap(card, c.deck == vocab::kMyDeck() ? "TỪ CỦA TÔI" : "TỪ VỰNG", ui::F().small, kMuted);
    lv_obj_set_style_text_letter_space(deck, 3, 0);
    Wrap(card, c.word, ui::F().reader(c.word.size() > 14 ? 48 : 64, true));
    if (!c.phonetic.empty()) Wrap(card, c.phonetic, ui::F().serif_italic, kMuted);
    if (!s->quiz && s->shown) {
        lv_obj_t* rule = Plain(card, 120, 2);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(rule, kHair, 0);
        Wrap(card, c.meaning, ui::F().reader(36, false));
        if (!c.example.empty()) Wrap(card, "“" + c.example + "”", ui::F().serif_italic, kMuted);
    } else if (!s->quiz) {
        Wrap(card, "Chạm để xem nghĩa", ui::F().small, kMuted);
    }

    if (s->quiz) {
        if (s->choices.empty()) MakeChoices(s);
        for (int k = 0; k < (int)s->choices.size(); ++k) {
            lv_obj_t* b = ui::Button(s->body, "", [s, k] {
                if (s->picked >= 0) return;
                s->picked = k;
                RenderCard(s);
            }, LV_PCT(100), 96);
            lv_obj_set_style_radius(b, 12, 0);
            lv_obj_set_style_pad_hor(b, 18, 0);
            lv_obj_t* l = ui::Label(b, std::string(1, (char)('A' + k)) + ".  " + s->choices[k], ui::F().body);
            lv_obj_set_width(l, LV_PCT(90));
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
            if (s->picked >= 0 && k == s->answer) {
                ui::Invert(b, true);
                lv_obj_align(ui::IconLabel(b, icon::CHECK), LV_ALIGN_RIGHT_MID, 0, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(b, -1), lv_color_white(), 0);
            } else if (s->picked == k) {
                lv_obj_set_style_border_width(b, 4, 0);
                lv_obj_align(ui::IconLabel(b, icon::X), LV_ALIGN_RIGHT_MID, 0, 0);
            }
        }
        if (s->picked >= 0) {
            const bool right = s->picked == s->answer;
            lv_obj_t* next = ui::Button(s->body, right ? "Đúng rồi · tiếp" : "Chưa đúng · tiếp", [s] {
                Grade(s, s->picked == s->answer ? vocab::Grade::Good : vocab::Grade::Again);
                s->choices.clear();
            }, LV_PCT(100), 80);
            ui::Invert(next, true);
        }
        ui::SetPageKeyHandler([s](int dir) {
            if (s->picked < 0) return;
            if (dir > 0) { Grade(s, s->picked == s->answer ? vocab::Grade::Good : vocab::Grade::Again); s->choices.clear(); }
        });
        return;
    }

    if (!s->shown) {
        lv_obj_t* show = ui::Button(s->body, "Hiện nghĩa", [s] { s->shown = true; RenderCard(s); }, LV_PCT(100), 96);
        ui::Invert(show, true);
    } else {
        lv_obj_t* row = Plain(s->body, LV_PCT(100), 112);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 10, 0);
        const std::pair<vocab::Grade, const char*> gs[] = {{vocab::Grade::Again, "Quên"}, {vocab::Grade::Hard, "Khó"},
                                                           {vocab::Grade::Good, "Nhớ"}, {vocab::Grade::Easy, "Dễ"}};
        const int bw = (ui::kW - 2 * ui::kPad - 30) / 4;
        for (auto& g : gs) {
            vocab::Grade gr = g.first;
            lv_obj_t* b = ui::Button(row, "", [s, gr] { Grade(s, gr); }, bw, 110);
            lv_obj_set_style_radius(b, 12, 0);
            if (gr == vocab::Grade::Good) ui::Invert(b, true);
            lv_obj_t* t = ui::Label(b, g.second, ui::F().body_bold);
            lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 14);
            int d = vocab::PreviewDays(c, gr);
            lv_obj_t* dl = ui::Label(b, d == 0 ? "< 1 ngày" : Days(d), ui::F().small);
            lv_obj_align(dl, LV_ALIGN_BOTTOM_MID, 0, -12);
            if (gr == vocab::Grade::Good) { lv_obj_set_style_text_color(t, lv_color_white(), 0); lv_obj_set_style_text_color(dl, lv_color_white(), 0); }
        }
    }
    std::string w = c.word;
    lv_obj_t* tools = Plain(s->body, LV_PCT(100), 70);
    lv_obj_t* look = ui::Button(tools, std::string(icon::DICT) + "  Tra từ điển", [w] { OpenDictionary(w); }, 300, 64);
    lv_obj_set_style_border_width(look, 1, 0);
    lv_obj_align(look, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* hint = ui::Label(tools, "Phím lật: xem nghĩa / Nhớ", ui::F().small);
    lv_obj_set_style_text_color(hint, kMuted, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, 0, 0);
    // Page keys: next = reveal, then "Nhớ"; previous = "Quên" once revealed.
    ui::SetPageKeyHandler([s](int dir) {
        if (!s->shown) { if (dir > 0) { s->shown = true; RenderCard(s); } return; }
        Grade(s, dir > 0 ? vocab::Grade::Good : vocab::Grade::Again);
    });
}

void StartStudy(bool quiz) {
    auto* s = new Study();
    s->quiz = quiz;
    s->cards = vocab::Session(g_deck, g_new_per_day);
    if (quiz && s->cards.size() < 4) {   // quiz works on any words of the deck
        std::vector<vocab::Card> all;
        for (const auto& d : vocab::Decks()) if (g_deck.empty() || d.file == g_deck) for (auto& c : vocab::Cards(d.file)) all.push_back(c);
        std::mt19937 rng((unsigned)vocab::Today());
        std::shuffle(all.begin(), all.end(), rng);
        if (all.size() > 10) all.resize(10);
        s->cards = all;
    }
    if (s->cards.empty()) { delete s; ui::Toast("Hôm nay không còn thẻ nào. Quay lại vào ngày mai nhé!"); return; }
    ui::Page p = ui::NewPage(quiz ? "TRẮC NGHIỆM" : "THẺ GHI NHỚ", [] { Home(); });
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) { delete static_cast<Study*>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, s);
    s->body = p.body;
    s->status = p.header_status;
    s->alive = ui::Alive(p.screen);
    lv_obj_remove_flag(p.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(p.body, 18, 0);
    RenderCard(s);
    ui::Show(p.screen);
}

void WordList(const std::string& file, const std::string& name, int page) {
    ui::Page p = ui::NewPage(name.c_str(), [] { Home(); });
    auto cards = vocab::Cards(file);
    const int per = 8, pages = std::max(1, ((int)cards.size() + per - 1) / per);
    page = std::clamp(page, 0, pages - 1);
    lv_label_set_text_fmt(p.header_status, "%d từ · trang %d / %d", (int)cards.size(), page + 1, pages);
    lv_obj_remove_flag(p.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(p.body, 0, 0);
    const int today = vocab::Today();
    for (int i = page * per; i < std::min((int)cards.size(), (page + 1) * per); ++i) {
        const auto& c = cards[i];
        std::string w = c.word;
        lv_obj_t* row = ui::Button(p.body, "", [w] { OpenDictionary(w); }, LV_PCT(100), 110);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, kHair, 0);
        lv_obj_set_style_pad_hor(row, 4, 0);
        lv_obj_t* wl = ui::Label(row, c.word + (c.phonetic.empty() ? "" : "  " + c.phonetic), ui::F().body_bold);
        lv_obj_set_width(wl, 460);
        lv_label_set_long_mode(wl, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(wl, LV_ALIGN_TOP_LEFT, 0, 12);
        lv_obj_t* ml = ui::Label(row, c.meaning, ui::F().small);
        lv_obj_set_width(ml, 560);
        lv_label_set_long_mode(ml, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(ml, LV_ALIGN_BOTTOM_LEFT, 0, -12);
        std::string st = !c.learned() ? "Mới" : c.due <= today ? "Ôn hôm nay" : "Ôn sau " + Days(c.due - today);
        lv_obj_t* sl = ui::Label(row, st, ui::F().small);
        lv_obj_set_style_text_color(sl, kMuted, 0);
        lv_obj_align(sl, LV_ALIGN_TOP_RIGHT, 0, 14);
    }
    lv_obj_t* pager = Plain(p.body, LV_PCT(100), 90);
    lv_obj_t* prev = ui::Button(pager, std::string(icon::CHEV_L) + " Trước", [file, name, page] { WordList(file, name, page - 1); }, 180, 64);
    lv_obj_align(prev, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t* next = ui::Button(pager, std::string("Sau ") + icon::CHEV_R, [file, name, page] { WordList(file, name, page + 1); }, 180, 64);
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    if (page == 0) lv_obj_add_state(prev, LV_STATE_DISABLED);
    if (page >= pages - 1) lv_obj_add_state(next, LV_STATE_DISABLED);
    ui::SetPageKeyHandler([file, name, page, pages](int dir) {
        int n = page + dir;
        if (n >= 0 && n < pages) WordList(file, name, n);
    });
    ui::Show(p.screen);
}

void Home() {
    ui::Page p = ui::NewPage("HỌC TỪ VỰNG", [] { OpenApps(); });
    auto decks = vocab::Decks();
    int due = 0, fresh = 0, learned = 0;
    for (const auto& d : decks) if (g_deck.empty() || d.file == g_deck) { due += d.due; fresh += d.fresh; learned += d.learned; }
    lv_label_set_text_fmt(p.header_status, "%d từ đã thuộc · chuỗi %d ngày · %d lượt hôm nay", learned, vocab::Streak(), vocab::StudiedToday());
    lv_obj_set_style_pad_row(p.body, 14, 0);

    // Today panel
    lv_obj_t* today = lv_obj_create(p.body);
    lv_obj_set_size(today, LV_PCT(100), 300);
    lv_obj_set_style_radius(today, 16, 0);
    lv_obj_set_style_border_width(today, 3, 0);
    lv_obj_set_style_pad_all(today, 24, 0);
    lv_obj_remove_flag(today, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* eb = ui::Label(today, "HÔM NAY", ui::F().small);
    lv_obj_set_style_text_letter_space(eb, 3, 0);
    lv_obj_set_style_text_color(eb, kMuted, 0);
    const int newc = std::min(fresh, g_new_per_day);
    char big[64];
    std::snprintf(big, sizeof big, "%d thẻ", due + newc);
    lv_obj_align(ui::Label(today, big, ui::F().reader(56, false)), LV_ALIGN_TOP_LEFT, 0, 30);
    char sub[96];
    std::snprintf(sub, sizeof sub, "%d cần ôn · %d từ mới", due, newc);
    lv_obj_t* sl = ui::Label(today, sub, ui::F().body);
    lv_obj_set_style_text_color(sl, kMuted, 0);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 0, 108);
    lv_obj_t* go = ui::Button(today, std::string(icon::BRAIN) + "  Học ngay", [] { StartStudy(false); }, 300, 76);
    ui::Invert(go, true);
    lv_obj_align(go, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t* qz = ui::Button(today, std::string(icon::TARGET) + "  Trắc nghiệm", [] { StartStudy(true); }, 280, 76);
    lv_obj_align(qz, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t* h = ui::Label(p.body, "BỘ TỪ", ui::F().small);
    lv_obj_set_style_text_letter_space(h, 2, 0);
    lv_obj_set_style_text_color(h, kMuted, 0);
    auto row = [&](const std::string& file, const std::string& name, int total, int d, int f) {
        const bool on = g_deck == file;
        lv_obj_t* b = ui::Button(p.body, "", [file] { g_deck = file; Home(); }, LV_PCT(100), 96);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, on ? 3 : 1, 0);
        lv_obj_set_style_border_color(b, on ? lv_color_black() : kHair, 0);
        lv_obj_set_style_pad_hor(b, 16, 0);
        lv_obj_align(ui::IconLabel(b, file == vocab::kMyDeck() ? icon::STAR : file.empty() ? icon::LAYERS : icon::BOOK), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* n = ui::Label(b, name, ui::F().body_bold);
        lv_obj_set_size(n, 460, lv_font_get_line_height(ui::F().body_bold));
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 60, 10);
        char c[96];
        std::snprintf(c, sizeof c, "%d từ · %d cần ôn · %d mới", total, d, f);
        lv_obj_t* cl = ui::Label(b, c, ui::F().small);
        lv_obj_set_style_text_color(cl, kMuted, 0);
        lv_obj_align(cl, LV_ALIGN_BOTTOM_LEFT, 60, -10);
        if (on) lv_obj_align(ui::IconLabel(b, icon::CHECK), LV_ALIGN_RIGHT_MID, 0, 0);
    };
    int total = 0, td = 0, tf = 0;
    for (const auto& d : decks) { total += d.total; td += d.due; tf += d.fresh; }
    row("", "Tất cả bộ từ", total, td, tf);
    for (const auto& d : decks) row(d.file, d.name, d.total, d.due, d.fresh);

    lv_obj_t* nh = ui::Label(p.body, "Từ mới mỗi ngày", ui::F().body_bold);
    (void)nh;
    lv_obj_t* chips = Plain(p.body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(chips, 10, 0);
    for (int n : {5, 10, 15, 20, 30}) {
        lv_obj_t* b = ui::Button(chips, std::to_string(n), [n] { g_new_per_day = n; Home(); }, 100, 64);
        lv_obj_set_style_radius(b, 32, 0);
        ui::Invert(b, n == g_new_per_day);
    }
    if (!g_deck.empty()) {
        std::string f = g_deck, nm;
        for (const auto& d : decks) if (d.file == f) nm = d.name;
        ui::Button(p.body, std::string(icon::LIST) + "  Danh sách từ của bộ này", [f, nm] { WordList(f, nm, 0); }, LV_PCT(100), 72);
    }
    lv_obj_t* hint = ui::Label(p.body, "Thêm bộ từ: tệp .tsv (từ<TAB>phiên âm<TAB>nghĩa<TAB>ví dụ) trong thư mục vocab/ trên thẻ. Từ tra khi đọc sách vào \"Từ của tôi\".", ui::F().small);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    ui::Show(p.screen);
}

}  // namespace

void OpenVocab() { Home(); }

}  // namespace screens
