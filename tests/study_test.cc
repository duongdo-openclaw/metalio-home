// Dictionary (StarDict + dictzip + TSV), notes (JSON + Markdown export) and vocabulary SRS.
#include <cstdlib>
#include <string>

#include "check.h"
#include "dictionary.h"
#include "notes.h"
#include "platform.h"
#include "vocab.h"

extern std::string g_test_sd;

int main() {
    char tmpl[] = "/tmp/metalio-study-XXXXXX";
    g_test_sd = mkdtemp(tmpl);
    platform::MakeDir(g_test_sd + "/.metalio");
    platform::MakeDir(g_test_sd + "/dict");
    // Fixture dictionaries (tools/make_sample_dict.py): StarDict .dict.dz + TSV.
    for (const char* f : {"en-vi-mau.ifo", "en-vi-mau.idx", "en-vi-mau.dict.dz", "tu-dien-viet-mau.tsv"}) {
        std::string d;
        CHECK(platform::ReadText(std::string(FIXTURES_DIR) + "/dict/" + f, d));
        platform::WriteText(g_test_sd + "/dict/" + f, d);
    }

    // ---- dictionary ----
    CHECK(dict::All().size() == 2);
    auto e = dict::LookupAll("Patience");                      // case-insensitive
    CHECK(!e.empty() && e[0].word == "patience" && e[0].text.find("kiên nhẫn") != std::string::npos);
    e = dict::LookupAll("“tortoises,”");                       // punctuation + plural
    CHECK(!e.empty() && e[0].word == "tortoise" && !e[0].via.empty());
    e = dict::LookupAll("studied");                            // -ied → -y
    CHECK(!e.empty() && e[0].word == "study");
    e = dict::LookupAll("laughed");                            // -ed
    CHECK(!e.empty() && e[0].word == "laugh");
    e = dict::LookupAll("tri thức");                           // TSV, Vietnamese phrase
    CHECK(!e.empty() && e[0].dict == "tu-dien-viet-mau");
    CHECK(dict::LookupAll("zzzzqx").empty());
    auto sug = dict::Suggest("pat", 5);
    CHECK(sug.size() >= 3 && sug[0] == "path");
    CHECK(dict::PlainText("<b>bold</b><br>line&amp;two", 'h') == "bold\nline&two");
    // every headword of the dictzip dictionary decodes (chunk boundaries, offsets)
    dict::Dictionary* sd = nullptr;
    for (auto* d : dict::All()) if (d->Name() == "Anh - Việt (mẫu)") sd = d;
    CHECK(sd && sd->WordCount() == 121);
    int ok = 0;
    for (auto& w : sd->Prefix("", 1000)) { std::string h, t; ok += sd->Lookup(w, h, t) && !t.empty(); }
    CHECK(ok == 121);

    // ---- notes ----
    notes::Note n;
    n.book = g_test_sd + "/books/Two Fables.txt";
    n.book_title = "Two Fables";
    n.chapter = 2;
    n.chapter_title = "The Fox";
    n.quote = "It is easy to dislike what you cannot have.";
    int id = notes::Add(n);
    notes::Note free;
    free.text = "Ghi chú riêng";
    notes::Add(free);
    CHECK(notes::All().size() == 2);
    CHECK(notes::ForBook(n.book).size() == 1);
    notes::Note got;
    CHECK(notes::Get(id, got) && got.quote == n.quote);
    got.text = "Bài học về sự kiên nhẫn";
    CHECK(notes::Update(got));
    std::string md;
    CHECK(platform::ReadText(g_test_sd + "/notes/Two Fables.md", md));
    CHECK(md.find("> It is easy") != std::string::npos && md.find("## The Fox") != std::string::npos &&
          md.find("kiên nhẫn") != std::string::npos);
    CHECK(notes::Remove(id));
    CHECK(!platform::FileExists(g_test_sd + "/notes/Two Fables.md"));   // export removed with the last note

    // ---- vocabulary ----
    auto decks = vocab::Decks();                               // seeds the starter deck
    CHECK(decks.size() == 1 && decks[0].total == 30 && decks[0].fresh == 30);
    CHECK(vocab::AddWord("tortoise", "/ˈtɔːtəs/", "con rùa", "The tortoise walked on."));
    CHECK(!vocab::AddWord("Tortoise", "", "", ""));            // no duplicates
    CHECK(vocab::HasWord("tortoise"));
    auto session = vocab::Session("", 5);
    CHECK(session.size() == 5);
    vocab::Card c = session[0];
    CHECK(vocab::PreviewDays(c, vocab::Grade::Again) == 0);
    CHECK(vocab::PreviewDays(c, vocab::Grade::Good) == 1);
    CHECK(vocab::PreviewDays(c, vocab::Grade::Easy) == 4);
    vocab::Review(c, vocab::Grade::Good);
    CHECK(c.reps == 1 && c.due == vocab::Today() + 1);
    vocab::Card again = session[1];
    vocab::Review(again, vocab::Grade::Again);                 // forgotten today: back in today's session
    auto s2 = vocab::Session("", 0);
    bool back = false;
    for (auto& x : s2) back |= x.word == again.word;
    CHECK(back);
    CHECK(vocab::StudiedToday() == 2 && vocab::Streak() == 1);
    // grows with successful reviews
    vocab::Card g = c;
    vocab::Review(g, vocab::Grade::Good);
    CHECK(g.interval == 3);
    vocab::Review(g, vocab::Grade::Good);
    CHECK(g.interval >= 7);
    vocab::Card line;
    CHECK(vocab::ParseLine("word\tmeaning", line) && line.meaning == "meaning" && line.phonetic.empty());
    CHECK(!vocab::ParseLine("# comment", line));

    std::string cmd = "rm -rf '" + g_test_sd + "'";
    if (g_test_sd.rfind("/tmp/metalio-study-", 0) == 0 && std::system(cmd.c_str()) != 0) std::printf("cleanup failed\n");
    DONE();
}
