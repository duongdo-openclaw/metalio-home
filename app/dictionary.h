// Offline dictionaries from the SD card (<sd>/dict):
//   * StarDict (.ifo + .idx + .dict or .dict.dz), the format KOReader, GoldenDict and most free
//     EN-VI / VI-EN dictionaries use. The .idx is kept in RAM (PSRAM on the device); definitions are
//     read on demand, from dictzip chunks for .dict.dz (random access, no full inflate).
//   * TSV: "word<TAB>definition" per line (\n inside a definition = new line), for small lists.
// Lookups ignore case and try simple English inflections (books → book, studied → study).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dict {

struct Entry {
    std::string dict;        // dictionary name (bookname)
    std::string word;        // headword found
    std::string text;        // plain-text definition
    std::string via;         // inflection rule used ("books → book"), empty for an exact hit
};

class Dictionary {
public:
    virtual ~Dictionary() = default;
    virtual const std::string& Name() const = 0;
    virtual size_t WordCount() const = 0;
    // Exact headword (case-insensitive). Returns false if absent.
    virtual bool Lookup(const std::string& word, std::string& headword, std::string& text) = 0;
    // Headwords starting with `prefix` (case-insensitive), at most `max`.
    virtual std::vector<std::string> Prefix(const std::string& prefix, size_t max) = 0;
};

// Opens one dictionary by any of its files (.ifo or .tsv). nullptr + err on failure.
std::unique_ptr<Dictionary> Open(const std::string& path, std::string& err);

// All dictionaries in <sd>/dict, opened once and cached (Reload() after the folder changes).
std::vector<Dictionary*>& All();
void Reload();

// Looks the word up in every dictionary: exact first, then inflections. Punctuation around the
// word (quotes, commas, “…”) is stripped first.
std::vector<Entry> LookupAll(const std::string& word);
std::string CleanWord(const std::string& raw);
std::vector<std::string> Suggest(const std::string& prefix, size_t max);

// StarDict definition fields → readable text (strips HTML/XDXF tags, decodes entities).
std::string PlainText(const std::string& data, char type);

}  // namespace dict
