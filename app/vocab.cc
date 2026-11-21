#include "vocab.h"

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>

#include "platform.h"

namespace vocab {
namespace {

struct Sched { int reps = 0, lapses = 0, interval = 0, ease = 250, due = 0; };
std::map<std::string, Sched> g_sched;          // "deck\x1fword" → schedule
std::map<int, int> g_days;                      // day → reviews
bool g_loaded = false;

std::string Key(const std::string& deck, const std::string& word) { return deck + "\x1f" + word; }
std::string File() { return platform::StateDir() + "/vocab.json"; }

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::string s;
    if (!platform::ReadText(File(), s)) return;
    cJSON* root = cJSON_Parse(s.c_str());
    const cJSON* cards = cJSON_GetObjectItem(root, "cards");
    const cJSON* it;
    cJSON_ArrayForEach(it, cards) {
        if (!cJSON_IsArray(it) || cJSON_GetArraySize(it) < 5 || !it->string) continue;
        Sched sc;
        sc.reps = cJSON_GetArrayItem(it, 0)->valueint;
        sc.lapses = cJSON_GetArrayItem(it, 1)->valueint;
        sc.interval = cJSON_GetArrayItem(it, 2)->valueint;
        sc.ease = cJSON_GetArrayItem(it, 3)->valueint;
        sc.due = cJSON_GetArrayItem(it, 4)->valueint;
        g_sched[it->string] = sc;
    }
    const cJSON* days = cJSON_GetObjectItem(root, "days");
    cJSON_ArrayForEach(it, days) if (it->string && cJSON_IsNumber(it)) g_days[std::atoi(it->string)] = it->valueint;
    cJSON_Delete(root);
}

void Save() {
    cJSON* root = cJSON_CreateObject();
    cJSON* cards = cJSON_AddObjectToObject(root, "cards");
    for (const auto& kv : g_sched) {
        const Sched& s = kv.second;
        int v[5] = {s.reps, s.lapses, s.interval, s.ease, s.due};
        cJSON_AddItemToObject(cards, kv.first.c_str(), cJSON_CreateIntArray(v, 5));
    }
    cJSON* days = cJSON_AddObjectToObject(root, "days");
    const int today = Today();
    for (const auto& kv : g_days)
        if (kv.first > today - 400) cJSON_AddNumberToObject(days, std::to_string(kv.first).c_str(), kv.second);
    char* out = cJSON_PrintUnformatted(root);
    if (out) platform::WriteText(File(), out);
    cJSON_free(out);
    cJSON_Delete(root);
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && s[a] == ' ') ++a;
    return s.substr(a);
}

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Starter deck written on first use (the user's own decks are any TSV files in vocab/).
const char* kStarter =
    "# Từ vựng tiếng Anh thông dụng (mẫu). Cột: từ<TAB>phiên âm<TAB>nghĩa<TAB>ví dụ\n"
    "habit\t/ˈhæbɪt/\tthói quen\tReading every night is a good habit.\n"
    "knowledge\t/ˈnɒlɪdʒ/\tkiến thức, tri thức\tBooks are a door to knowledge.\n"
    "patient\t/ˈpeɪʃnt/\tkiên nhẫn\tBe patient: good things take time.\n"
    "achieve\t/əˈtʃiːv/\tđạt được\tShe achieved her reading goal this year.\n"
    "improve\t/ɪmˈpruːv/\tcải thiện, tiến bộ\tYour English will improve step by step.\n"
    "curious\t/ˈkjʊəriəs/\ttò mò, ham hiểu biết\tA curious mind keeps learning.\n"
    "borrow\t/ˈbɒrəʊ/\tmượn\tCan I borrow this book?\n"
    "author\t/ˈɔːθə/\ttác giả\tWho is the author of this novel?\n"
    "chapter\t/ˈtʃæptə/\tchương (sách)\tI read one chapter before bed.\n"
    "novel\t/ˈnɒvl/\ttiểu thuyết\tThis novel has five hundred pages.\n"
    "library\t/ˈlaɪbrəri/\tthư viện\tThe library opens at eight.\n"
    "explain\t/ɪkˈspleɪn/\tgiải thích\tCan you explain this word?\n"
    "remember\t/rɪˈmembə/\tnhớ\tI always remember her advice.\n"
    "forget\t/fəˈɡet/\tquên\tDon't forget to review your words.\n"
    "decide\t/dɪˈsaɪd/\tquyết định\tWe decided to start early.\n"
    "journey\t/ˈdʒɜːni/\tchuyến đi, hành trình\tLearning is a long journey.\n"
    "brave\t/breɪv/\tdũng cảm\tThe brave girl spoke first.\n"
    "honest\t/ˈɒnɪst/\ttrung thực\tAn honest answer is the best.\n"
    "quiet\t/ˈkwaɪət/\tyên tĩnh\tI like a quiet place to read.\n"
    "wisdom\t/ˈwɪzdəm/\tsự khôn ngoan\tWisdom comes with experience.\n"
    "effort\t/ˈefət/\tsự nỗ lực\tIt takes effort to learn a language.\n"
    "progress\t/ˈprəʊɡres/\tsự tiến bộ\tTrack your progress every week.\n"
    "focus\t/ˈfəʊkəs/\ttập trung\tTurn off your phone and focus.\n"
    "imagine\t/ɪˈmædʒɪn/\ttưởng tượng\tImagine a world without books.\n"
    "discover\t/dɪˈskʌvə/\tkhám phá\tYou discover new ideas in every book.\n"
    "lesson\t/ˈlesn/\tbài học\tEvery story has a lesson.\n"
    "simple\t/ˈsɪmpl/\tđơn giản\tKeep your notes short and simple.\n"
    "careful\t/ˈkeəfl/\tcẩn thận\tBe careful with new words.\n"
    "grateful\t/ˈɡreɪtfl/\tbiết ơn\tI am grateful for your help.\n"
    "succeed\t/səkˈsiːd/\tthành công\tSlow and steady, you will succeed.\n";

void Seed() {
    const std::string dir = platform::VocabDir();
    platform::MakeDir(dir);
    if (platform::ListFiles(dir, {".tsv"}).empty())
        platform::WriteText(dir + "/tieng-anh-thong-dung.tsv", kStarter);
}

std::string DeckName(const std::string& file) {
    if (file == kMyDeck()) return "Từ của tôi";
    // A first comment line names the deck: "# Từ vựng tiếng Anh thông dụng (mẫu). Cột: …"
    std::string head;
    if (platform::ReadRange(platform::VocabDir() + "/" + file, 0, 240, head) && head.size() > 2 && head[0] == '#') {
        head = head.substr(1, head.find('\n') == std::string::npos ? std::string::npos : head.find('\n') - 1);
        size_t cut = std::min(head.find(". "), head.find(':'));
        if (cut != std::string::npos) head = head.substr(0, cut);
        head = Trim(head);
        if (!head.empty() && head.size() <= 80) return head;
    }
    std::string n = file.substr(0, file.rfind('.'));
    for (auto& c : n) if (c == '-' || c == '_') c = ' ';
    if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
    return n;
}

void Apply(Card& c) {
    auto it = g_sched.find(Key(c.deck, c.word));
    if (it == g_sched.end()) return;
    c.reps = it->second.reps;
    c.lapses = it->second.lapses;
    c.interval = it->second.interval;
    c.ease = it->second.ease;
    c.due = it->second.due;
}

void Schedule(Card& c, Grade g) {
    // SM-2 with four buttons (Anki-like). New/lapsed cards come back the same day on "Quên".
    if (g == Grade::Again) {
        c.lapses += c.reps > 0 ? 1 : 0;
        c.interval = 0;
        c.ease = std::max(130, c.ease - 20);
    } else if (c.reps == 0) {
        c.interval = g == Grade::Hard ? 1 : g == Grade::Good ? 1 : 4;
    } else if (c.reps == 1 && g != Grade::Easy) {
        c.interval = g == Grade::Hard ? 2 : 3;
    } else {
        double f = g == Grade::Hard ? 1.2 : g == Grade::Good ? c.ease / 100.0 : c.ease / 100.0 * 1.3;
        c.interval = std::max(c.interval + 1, (int)(c.interval * f + 0.5));
    }
    if (g == Grade::Hard) c.ease = std::max(130, c.ease - 15);
    if (g == Grade::Easy) c.ease += 15;
    if (g != Grade::Again) c.reps += 1;
    c.interval = std::min(c.interval, 3650);
    c.due = Today() + c.interval;
}

}  // namespace

const char* kMyDeck() { return "my-words.tsv"; }

int Today() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    tm.tm_hour = 12; tm.tm_min = 0; tm.tm_sec = 0;
    return (int)(std::mktime(&tm) / 86400);
}

bool ParseLine(const std::string& raw, Card& c) {
    std::string line = Trim(raw);
    if (line.empty() || line[0] == '#') return false;
    std::vector<std::string> col;
    size_t p = 0;
    while (true) {
        size_t t = line.find('\t', p);
        col.push_back(Trim(line.substr(p, t == std::string::npos ? std::string::npos : t - p)));
        if (t == std::string::npos) break;
        p = t + 1;
    }
    if (col[0].empty()) return false;
    c.word = col[0];
    if (col.size() == 2) { c.meaning = col[1]; return true; }   // "word<TAB>meaning"
    if (col.size() > 1) c.phonetic = col[1];
    if (col.size() > 2) c.meaning = col[2];
    if (col.size() > 3) c.example = col[3];
    for (size_t k; (k = c.meaning.find("\\n")) != std::string::npos;) c.meaning.replace(k, 2, "\n");
    return !c.meaning.empty() || !c.phonetic.empty();
}

std::vector<Card> Cards(const std::string& deck) {
    Load();
    std::vector<Card> out;
    std::string all;
    if (!platform::ReadText(platform::VocabDir() + "/" + deck, all)) return out;
    size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find('\n', p);
        Card c;
        c.deck = deck;
        if (ParseLine(all.substr(p, e == std::string::npos ? std::string::npos : e - p), c)) { Apply(c); out.push_back(c); }
        if (e == std::string::npos) break;
        p = e + 1;
    }
    return out;
}

std::vector<Deck> Decks() {
    Seed();
    std::vector<Deck> out;
    const int today = Today();
    std::vector<std::string> files = platform::ListFiles(platform::VocabDir(), {".tsv"});
    auto mine = std::find(files.begin(), files.end(), kMyDeck());
    if (mine != files.end()) { files.erase(mine); files.insert(files.begin(), kMyDeck()); }
    for (const auto& f : files) {
        Deck d;
        d.file = f;
        d.name = DeckName(f);
        for (const auto& c : Cards(f)) {
            d.total++;
            if (!c.learned()) d.fresh++;
            else { d.learned++; if (c.due <= today) d.due++; }
        }
        out.push_back(d);
    }
    return out;
}

std::vector<Card> Session(const std::string& deck, int new_limit) {
    std::vector<Card> due, fresh;
    const int today = Today();
    std::vector<std::string> files = deck.empty() ? platform::ListFiles(platform::VocabDir(), {".tsv"}) : std::vector<std::string>{deck};
    for (const auto& f : files)
        for (const auto& c : Cards(f)) {
            if (c.learned() && c.due <= today) due.push_back(c);
            else if (!c.learned() && c.interval == 0 && g_sched.count(Key(c.deck, c.word))) due.push_back(c);   // "Quên" earlier today
            else if (!c.learned()) fresh.push_back(c);
        }
    std::sort(due.begin(), due.end(), [](const Card& a, const Card& b) { return a.due < b.due; });
    for (int i = 0; i < (int)fresh.size() && i < new_limit; ++i) due.push_back(fresh[i]);
    return due;
}

int PreviewDays(const Card& c, Grade g) {
    Card x = c;
    Schedule(x, g);
    return x.interval;
}

void Review(Card& c, Grade g) {
    Load();
    Schedule(c, g);
    Sched& s = g_sched[Key(c.deck, c.word)];
    s.reps = c.reps; s.lapses = c.lapses; s.interval = c.interval; s.ease = c.ease; s.due = c.due;
    g_days[Today()]++;
    Save();
}

bool HasWord(const std::string& word) {
    for (const auto& c : Cards(kMyDeck())) if (Lower(c.word) == Lower(word)) return true;
    return false;
}

bool AddWord(const std::string& word, const std::string& phonetic, const std::string& meaning, const std::string& example) {
    if (word.empty() || HasWord(word)) return false;
    platform::MakeDir(platform::VocabDir());
    const std::string path = platform::VocabDir() + "/" + kMyDeck();
    std::string all;
    platform::ReadText(path, all);
    if (all.empty()) all = "# Từ của tôi: các từ đã tra khi đọc sách. Cột: từ<TAB>phiên âm<TAB>nghĩa<TAB>ví dụ\n";
    if (all.back() != '\n') all += '\n';
    auto clean = [](std::string s) {
        for (auto& c : s) if (c == '\t') c = ' ';
        std::string o;
        for (char c : s) { if (c == '\n') o += "\\n"; else if (c != '\r') o += c; }
        return o;
    };
    all += clean(word) + "\t" + clean(phonetic) + "\t" + clean(meaning) + "\t" + clean(example) + "\n";
    return platform::WriteText(path, all);
}

int StudiedToday() {
    Load();
    auto it = g_days.find(Today());
    return it == g_days.end() ? 0 : it->second;
}

int Streak() {
    Load();
    int d = Today(), n = 0;
    if (!g_days.count(d)) --d;   // today not studied yet still keeps yesterday's streak
    while (g_days.count(d)) { ++n; --d; }
    return n;
}

}  // namespace vocab
