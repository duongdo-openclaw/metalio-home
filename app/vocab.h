// "Học từ vựng": word decks on the SD card (<sd>/vocab/*.tsv: word, phonetic, meaning, example)
// studied with spaced repetition (SM-2 style: Quên / Khó / Nhớ / Dễ). Words looked up while
// reading go to the "Từ của tôi" deck (my-words.tsv). Progress: <sd>/.metalio/vocab.json.
#pragma once
#include <string>
#include <vector>

namespace vocab {

struct Card {
    std::string deck;            // file name, e.g. "my-words.tsv"
    std::string word, phonetic, meaning, example;
    // scheduling
    int reps = 0, lapses = 0;
    int interval = 0;            // days
    int ease = 250;              // x100
    int due = 0;                 // day number (days since 1970, local)
    bool learned() const { return reps > 0; }
};

struct Deck {
    std::string file, name;
    int total = 0, fresh = 0, due = 0, learned = 0;
};

enum class Grade { Again = 0, Hard = 1, Good = 2, Easy = 3 };

int Today();                                     // local day number
std::vector<Deck> Decks();                       // all decks with counts (seeds a starter deck)
std::vector<Card> Cards(const std::string& deck_file);
// Today's session: due cards first, then up to `new_limit` new ones. deck "" = all decks.
std::vector<Card> Session(const std::string& deck_file, int new_limit);
void Review(Card& c, Grade g);                   // schedules + saves
int PreviewDays(const Card& c, Grade g);         // interval the grade would give (button labels)
bool AddWord(const std::string& word, const std::string& phonetic, const std::string& meaning,
             const std::string& example);       // to "Từ của tôi"; false if already there
bool HasWord(const std::string& word);
int StudiedToday();
int Streak();
const char* kMyDeck();

// Parses a deck line "word\tphonetic\tmeaning\texample" (missing columns allowed).
bool ParseLine(const std::string& line, Card& out);

}  // namespace vocab
