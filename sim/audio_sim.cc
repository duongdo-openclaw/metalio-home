// Simulator audio: SDL2 playback (MP3 via minimp3, 16-bit PCM WAV) and microphone capture to WAV.
#define MINIMP3_IMPLEMENTATION
#include "../third_party/minimp3/minimp3_ex.h"

#include <SDL2/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "../app/platform.h"
#include "platform_sim.h"

namespace platform {
namespace {

std::mutex g_mu;  // guards everything below (SDL callbacks run on SDL's audio thread)

// ---- playback ----
SDL_AudioDeviceID g_out = 0;
SDL_AudioSpec g_out_spec{};
std::vector<int16_t> g_pcm;      // already converted to the device format (interleaved)
size_t g_pos = 0;                // in samples (not frames)
PlayStatus g_play;
bool g_fake_mic = false;

void OutCallback(void*, Uint8* stream, int len) {
    std::lock_guard<std::mutex> l(g_mu);
    auto* out = reinterpret_cast<int16_t*>(stream);
    size_t n = (size_t)len / 2;
    std::memset(stream, 0, (size_t)len);
    if (g_play.state != PlayState::Playing) return;
    float vol = g_play.volume / 100.0f;
    vol *= vol;  // perceptual
    size_t i = 0;
    for (; i < n && g_pos < g_pcm.size(); ++i, ++g_pos) out[i] = (int16_t)(g_pcm[g_pos] * vol);
    if (g_pos >= g_pcm.size()) g_play.state = PlayState::Ended;
}

bool EnsureOutput(std::string& err) {
    if (g_out) return true;
    if (!(SDL_WasInit(SDL_INIT_AUDIO))) SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec want{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = OutCallback;
    g_out = SDL_OpenAudioDevice(nullptr, 0, &want, &g_out_spec, 0);
    if (!g_out) { err = SDL_GetError(); return false; }
    SDL_PauseAudioDevice(g_out, 0);
    return true;
}

bool LoadWav(const std::string& path, std::vector<int16_t>& pcm, int& rate, int& ch, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    unsigned char h[12];
    if (!f.read((char*)h, 12) || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4)) { err = "không phải WAV"; return false; }
    int bits = 0;
    while (f) {
        unsigned char c[8];
        if (!f.read((char*)c, 8)) break;
        uint32_t sz = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!std::memcmp(c, "fmt ", 4)) {
            std::vector<unsigned char> fmt(sz);
            f.read((char*)fmt.data(), sz);
            ch = fmt[2] | fmt[3] << 8;
            rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16 | fmt[7] << 24;
            bits = fmt[14] | fmt[15] << 8;
        } else if (!std::memcmp(c, "data", 4)) {
            if (bits != 16) { err = "chỉ hỗ trợ WAV 16-bit"; return false; }
            pcm.resize(sz / 2);
            f.read((char*)pcm.data(), sz & ~1u);
            return ch > 0 && rate > 0;
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);
        }
    }
    err = "WAV thiếu dữ liệu";
    return false;
}

bool Convert(const std::vector<int16_t>& in, int rate, int ch, std::vector<int16_t>& out) {
    SDL_AudioStream* s = SDL_NewAudioStream(AUDIO_S16SYS, (Uint8)ch, rate, g_out_spec.format, g_out_spec.channels, g_out_spec.freq);
    if (!s) return false;
    SDL_AudioStreamPut(s, in.data(), (int)(in.size() * 2));
    SDL_AudioStreamFlush(s);
    out.resize((size_t)SDL_AudioStreamAvailable(s) / 2);
    SDL_AudioStreamGet(s, out.data(), (int)(out.size() * 2));
    SDL_FreeAudioStream(s);
    return true;
}

// ---- recording ----
SDL_AudioDeviceID g_in = 0;
std::vector<int16_t> g_rec;
RecStatus g_recst;
std::thread g_fake;
std::atomic<bool> g_fake_run{false};
constexpr int kRecRate = 16000;

void Append(const int16_t* s, size_t n) {
    // caller holds g_mu
    if (g_recst.state != RecState::Recording) return;
    int peak = 0;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, std::abs((int)s[i]));
    g_recst.level = std::min(100, peak * 100 / 20000);
    g_rec.insert(g_rec.end(), s, s + n);
    g_recst.duration_ms = g_rec.size() * 1000 / kRecRate;
}

void InCallback(void*, Uint8* stream, int len) {
    std::lock_guard<std::mutex> l(g_mu);
    Append(reinterpret_cast<int16_t*>(stream), (size_t)len / 2);
}

void FakeMic() {  // speech-like test signal: a 220 Hz tone with a syllable envelope + noise
    uint64_t t = 0;
    while (g_fake_run) {
        std::vector<int16_t> buf(kRecRate / 10);
        for (auto& v : buf) {
            double env = 0.5 + 0.5 * std::sin(2 * M_PI * 3.0 * t / kRecRate);
            v = (int16_t)(env * 9000 * std::sin(2 * M_PI * 220.0 * t / kRecRate) + (std::rand() % 600 - 300));
            ++t;
        }
        {
            std::lock_guard<std::mutex> l(g_mu);
            Append(buf.data(), buf.size());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void StopInput() {
    if (g_in) { SDL_CloseAudioDevice(g_in); g_in = 0; }
    if (g_fake_run) { g_fake_run = false; if (g_fake.joinable()) g_fake.join(); }
}

void Put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += (char)(v >> (8 * i)); }
void Put16(std::string& s, uint16_t v) { s += (char)v; s += (char)(v >> 8); }

}  // namespace

bool AudioPlay(const std::string& path) {
    std::string err;
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_play = PlayStatus{PlayState::Loading, path, 0, 0, g_play.volume, ""};
    }
    if (!EnsureOutput(err)) {
        std::lock_guard<std::mutex> l(g_mu);
        g_play.state = PlayState::Error;
        g_play.error = "không mở được loa: " + err;
        return false;
    }
    std::vector<int16_t> src;
    int rate = 0, ch = 0;
    std::string lower = path;
    for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
    bool ok;
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".wav") {
        ok = LoadWav(path, src, rate, ch, err);
    } else {
        mp3dec_t dec;
        mp3dec_file_info_t info{};
        ok = mp3dec_load(&dec, path.c_str(), &info, nullptr, nullptr) == 0 && info.samples > 0;
        if (ok) {
            src.assign(info.buffer, info.buffer + info.samples);
            rate = info.hz;
            ch = info.channels;
        } else {
            err = "không giải mã được MP3";
        }
        std::free(info.buffer);
    }
    std::vector<int16_t> pcm;
    if (ok) ok = Convert(src, rate, ch, pcm);
    std::lock_guard<std::mutex> l(g_mu);
    if (!ok) {
        g_play.state = PlayState::Error;
        g_play.error = err;
        return false;
    }
    g_pcm = std::move(pcm);
    g_pos = 0;
    g_play.duration_ms = (uint64_t)g_pcm.size() * 1000 / ((uint64_t)g_out_spec.freq * g_out_spec.channels);
    g_play.state = PlayState::Playing;
    Log("[audio] play %s (%d Hz, %d ch, %llu ms)", path.c_str(), rate, ch, (unsigned long long)g_play.duration_ms);
    return true;
}

void AudioPause(bool paused) {
    std::lock_guard<std::mutex> l(g_mu);
    if (paused && g_play.state == PlayState::Playing) g_play.state = PlayState::Paused;
    else if (!paused && g_play.state == PlayState::Paused) g_play.state = PlayState::Playing;
}

void AudioStop() {
    std::lock_guard<std::mutex> l(g_mu);
    g_play.state = PlayState::Stopped;
    g_play.path.clear();
    g_pcm.clear();
    g_pos = 0;
}

void AudioSeek(uint64_t ms) {
    std::lock_guard<std::mutex> l(g_mu);
    size_t p = (size_t)(ms * g_out_spec.freq / 1000) * g_out_spec.channels;
    g_pos = std::min(p, g_pcm.size());
}

void AudioSetVolume(int v) {
    std::lock_guard<std::mutex> l(g_mu);
    g_play.volume = std::max(0, std::min(100, v));
}

PlayStatus AudioStatus() {
    std::lock_guard<std::mutex> l(g_mu);
    PlayStatus s = g_play;
    if (g_out_spec.freq) s.position_ms = (uint64_t)g_pos * 1000 / ((uint64_t)g_out_spec.freq * g_out_spec.channels);
    return s;
}

bool RecStart(const std::string& path) {
    StopInput();
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_rec.clear();
        g_recst = RecStatus{};
        g_recst.path = path;
    }
    if (!(SDL_WasInit(SDL_INIT_AUDIO))) SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!g_fake_mic) {
        SDL_AudioSpec want{}, have{};
        want.freq = kRecRate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = InCallback;
        g_in = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
        if (!g_in) {
            std::lock_guard<std::mutex> l(g_mu);
            g_recst.state = RecState::Error;
            g_recst.error = std::string("không có micro (") + SDL_GetError() + ")";
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_recst.state = RecState::Recording;
    }
    if (g_fake_mic) { g_fake_run = true; g_fake = std::thread(FakeMic); }
    else SDL_PauseAudioDevice(g_in, 0);
    Log("[audio] recording to %s%s", path.c_str(), g_fake_mic ? " (fake mic)" : "");
    return true;
}

void RecPause(bool paused) {
    std::lock_guard<std::mutex> l(g_mu);
    if (paused && g_recst.state == RecState::Recording) { g_recst.state = RecState::Paused; g_recst.level = 0; }
    else if (!paused && g_recst.state == RecState::Paused) g_recst.state = RecState::Recording;
}

bool RecStop() {
    StopInput();
    std::string wav;
    std::string path;
    {
        std::lock_guard<std::mutex> l(g_mu);
        path = g_recst.path;
        uint32_t data = (uint32_t)(g_rec.size() * 2);
        wav = "RIFF";
        Put32(wav, 36 + data);
        wav += "WAVEfmt ";
        Put32(wav, 16); Put16(wav, 1); Put16(wav, 1); Put32(wav, kRecRate); Put32(wav, kRecRate * 2);
        Put16(wav, 2); Put16(wav, 16);
        wav += "data";
        Put32(wav, data);
        wav.append(reinterpret_cast<const char*>(g_rec.data()), data);
    }
    bool ok = WriteText(path, wav);
    std::lock_guard<std::mutex> l(g_mu);
    g_recst.state = ok ? RecState::Saved : RecState::Error;
    if (!ok) g_recst.error = "không ghi được tệp";
    g_recst.level = 0;
    return ok;
}

RecStatus RecGetStatus() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_recst;
}

}  // namespace platform

void sim_audio_set_fake_mic(bool fake) { platform::g_fake_mic = fake; }

void sim_audio_shutdown() {
    platform::StopInput();
    if (platform::g_out) SDL_CloseAudioDevice(platform::g_out);
    platform::g_out = 0;
}
