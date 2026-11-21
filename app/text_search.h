// Accent- and case-insensitive search for Vietnamese text: "khong dau" finds "không dấu".
// Typing Vietnamese on an e-ink keyboard is slow, so the default search ignores diacritics.
#pragma once
#include <string>
#include <vector>

namespace search {

// Folds UTF-8 to lower-case ASCII where possible (đ->d, ế->e, Ư->u …). `map` (optional) receives,
// for every output byte, the byte offset of the source character it came from.
std::string Fold(const std::string& s, std::vector<size_t>* map = nullptr);

// Byte offsets (in `text`) of every match of `query`, compared after folding.
std::vector<size_t> FindAll(const std::string& text, const std::string& query);

}  // namespace search
