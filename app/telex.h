// Vietnamese Telex typing for the on-screen keyboard (notes, dictionary, search):
// aa→â aw→ă ee→ê oo→ô ow→ơ uw→ư w→ư dd→đ, tones s f r x j (z removes), the tone lands on the
// right vowel (classic placement: hòa, khỏe, của, quá, già, đường, nguyễn). Pressing the same tone
// key twice cancels it and types the letter ("ass" → "as").
#pragma once
#include <string>

namespace telex {

// `word` is the word before the cursor (UTF-8, may already carry marks); returns the new word
// after typing ASCII character `c`.
std::string Type(const std::string& word, char c);

// Convenience for tests: feed a whole ASCII string through Type, word by word.
std::string Convert(const std::string& ascii);

}  // namespace telex
