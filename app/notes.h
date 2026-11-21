// Notes ("Ghi chú"): quotes highlighted in books (with an optional comment) and free notes.
// Stored as JSON in <sd>/.metalio/notes.json; every change also rewrites <sd>/notes/<book>.md so
// notes can be read on a computer (USB / Wi-Fi transfer) without the device.
#pragma once
#include <string>
#include <vector>

namespace notes {

struct Note {
    int id = 0;
    std::string book;            // book path; empty for a free note
    std::string book_title;
    std::string chapter_title;
    int chapter = 0;
    int para = 0;                // start of the quote
    size_t offset = 0;
    int para_end = 0;            // end of the quote (exclusive offset)
    size_t offset_end = 0;
    std::string quote;           // highlighted text (empty for a free note)
    std::string text;            // the reader's own words
    long long created = 0, updated = 0;
};

std::vector<Note> All();                         // newest first
std::vector<Note> ForBook(const std::string& book_path);
bool Get(int id, Note& out);
int Add(Note n);                                 // returns the new id
bool Update(const Note& n);
bool Remove(int id);
std::string Markdown(const std::string& book_path);   // export of one book's notes
std::string ExportName(const Note& n);                 // notes/<file>.md for this note's book

}  // namespace notes
