// AI assistant client: ChatGPT (OpenAI), Gemini (Google), Claude (Anthropic), DeepSeek, Grok (xAI),
// Mistral, Groq, OpenRouter, or any OpenAI-compatible server (Ollama, LM Studio…).
// Pure request building / response parsing (unit tested); the HTTP call goes through
// platform::Http. The API key lives on the SD card (<sd>/.metalio/ai.json), never in the firmware.
#pragma once
#include <string>
#include <vector>

#include "platform.h"

namespace ai {

// Stored by id in ai.json; append only.
enum class Provider { OpenAI, Gemini, Compatible, Claude, DeepSeek, Grok, Mistral, Groq, OpenRouter, Count };
enum class Wire { OpenAI, Gemini, Anthropic };   // request format

struct ProviderInfo {
    Provider id;
    const char* key;          // ai.json value
    const char* name;         // shown in Cài đặt
    Wire wire;
    const char* base_url;     // OpenAI wire: …/v1 (empty = user supplies, Compatible)
    const char* model;        // default model (editable: providers rename models often)
    const char* stt_model;    // speech-to-text model, "" = no voice input with this provider
    const char* key_hint;     // where to get a key
};
const ProviderInfo& Info(Provider p);

struct Config {
    Provider provider = Provider::OpenAI;
    std::string api_key;
    std::string model;            // empty = provider default
    std::string base_url;         // Compatible only, e.g. http://192.168.1.10:11434/v1
    std::string transcribe_model; // OpenAI speech-to-text, empty = default
    bool voice = true;            // hold the AI key to talk
    bool configured() const { return !api_key.empty() || provider == Provider::Compatible; }
    bool voice_supported() const { return *Info(provider).stt_model || !transcribe_model.empty(); }
};

struct Message {
    std::string role;             // "user" or "assistant"
    std::string text;
};

// What the reader is looking at, sent as context so "giải thích đoạn này" makes sense.
struct Context {
    std::string book_title, chapter, selection, page_text;
};

Config Load();
void Save(const Config& c);
const char* ProviderName(Provider p);
std::string DefaultModel(Provider p);
std::string SystemPrompt(const Context& ctx);
std::string MaskKey(const std::string& key);   // "sk-…abcd"

// Chat completion request (headers carry the key; JSON body).
platform::HttpRequest ChatRequest(const Config& c, const std::vector<Message>& history, const Context& ctx);
// Extracts the answer text; false + error message on API errors.
bool ParseChat(Provider p, int status, const std::string& body, std::string& text, std::string& error);

// Voice: speech to text. OpenAI: multipart /audio/transcriptions; Gemini: inline audio.
// Compatible servers use the OpenAI form.
platform::HttpRequest TranscribeRequest(const Config& c, const std::string& wav);
bool ParseTranscript(Provider p, int status, const std::string& body, std::string& text, std::string& error);

std::string Base64(const std::string& data);

}  // namespace ai
