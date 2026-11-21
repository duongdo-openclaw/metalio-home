// Title/artist from ID3v2 tags (MP3) and duration of 16-bit PCM WAV files. Pure C++.
#pragma once
#include <cstdint>
#include <string>

namespace media {

struct Info {
    std::string title, artist;
    uint64_t duration_ms = 0;   // WAV only (MP3 needs decoding; the player reports it)
};

Info Read(const std::string& path);
std::string DisplayName(const std::string& file_name);   // "cartesia_lien_vi.mp3" -> "cartesia lien vi"

}  // namespace media
