// Request building and response parsing for every AI provider wire format (no network).
#include <string>

#include "ai_client.h"
#include "check.h"

static bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
static std::string Header(const platform::HttpRequest& r, const std::string& k) {
    for (auto& h : r.headers) if (h.first == k) return h.second;
    return "";
}

int main() {
    ai::Context ctx;
    ctx.book_title = "Two Short Fables";
    ctx.selection = "Slow and steady wins the race.";
    std::vector<ai::Message> h = {{"user", "Giải thích câu này"}};

    // ChatGPT
    ai::Config c;
    c.api_key = "sk-test1234";
    auto r = ai::ChatRequest(c, h, ctx);
    CHECK(r.url == "https://api.openai.com/v1/chat/completions");
    CHECK(Header(r, "Authorization") == "Bearer sk-test1234");
    CHECK(Has(r.body, "\"model\":\"gpt-4o-mini\"") && Has(r.body, "\"role\":\"system\"") && Has(r.body, "Slow and steady"));
    std::string t, e;
    CHECK(ai::ParseChat(c.provider, 200, R"({"choices":[{"message":{"role":"assistant","content":"  Chậm mà chắc.\n"}}]})", t, e) && t == "Chậm mà chắc.");
    CHECK(!ai::ParseChat(c.provider, 401, R"({"error":{"message":"Incorrect API key"}})", t, e) && Has(e, "Khoá API"));
    CHECK(!ai::ParseChat(c.provider, 0, "", t, e) && Has(e, "Không kết nối"));

    // Gemini
    c.provider = ai::Provider::Gemini;
    r = ai::ChatRequest(c, {{"user", "a"}, {"assistant", "b"}, {"user", "c"}}, ctx);
    CHECK(r.url == "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent");
    CHECK(Header(r, "x-goog-api-key") == "sk-test1234" && Has(r.body, "\"role\":\"model\"") && Has(r.body, "system_instruction"));
    CHECK(ai::ParseChat(c.provider, 200, R"({"candidates":[{"content":{"parts":[{"text":"Xin "},{"text":"chào"}]}}]})", t, e) && t == "Xin chào");
    CHECK(!ai::ParseChat(c.provider, 200, R"({"candidates":[{"finishReason":"SAFETY"}]})", t, e) && Has(e, "từ chối"));

    // Claude (Messages API)
    c.provider = ai::Provider::Claude;
    r = ai::ChatRequest(c, h, ctx);
    CHECK(r.url == "https://api.anthropic.com/v1/messages");
    CHECK(Header(r, "x-api-key") == "sk-test1234" && Header(r, "anthropic-version") == "2026-10-21");
    CHECK(Has(r.body, "\"model\":\"claude-opus-5-5\"") && Has(r.body, "\"system\":") && Has(r.body, "\"max_tokens\":16000"));
    CHECK(Has(r.body, "\"fallbacks\":\"default\"") && Header(r, "anthropic-beta") == "server-side-fallback-2026-07-01");
    CHECK(ai::ParseChat(c.provider, 200, R"({"content":[{"type":"thinking","thinking":""},{"type":"text","text":"Kiên nhẫn thắng."}],"stop_reason":"end_turn"})", t, e) && t == "Kiên nhẫn thắng.");
    CHECK(!ai::ParseChat(c.provider, 200, R"({"content":[],"stop_reason":"refusal"})", t, e) && Has(e, "từ chối"));

    // OpenAI-compatible presets and a private server
    c.provider = ai::Provider::DeepSeek;
    CHECK(ai::ChatRequest(c, h, ctx).url == "https://api.deepseek.com/v1/chat/completions");
    c.provider = ai::Provider::Groq;
    CHECK(ai::ChatRequest(c, h, ctx).url == "https://api.groq.com/openai/v1/chat/completions");
    CHECK(c.voice_supported());
    c.provider = ai::Provider::Claude;
    CHECK(!c.voice_supported());
    ai::Config local;
    local.provider = ai::Provider::Compatible;
    local.base_url = "http://192.168.1.10:11434/v1/";
    r = ai::ChatRequest(local, h, ctx);
    CHECK(r.url == "http://192.168.1.10:11434/v1/chat/completions" && Header(r, "Authorization").empty());
    CHECK(local.configured());

    // Voice
    c.provider = ai::Provider::OpenAI;
    r = ai::TranscribeRequest(c, std::string("RIFF\0\0WAVE", 10));
    CHECK(r.url == "https://api.openai.com/v1/audio/transcriptions" && Has(r.content_type, "multipart/form-data; boundary="));
    CHECK(Has(r.body, "gpt-4o-mini-transcribe") && Has(r.body, "filename=\"voice.wav\"") && r.body.find(std::string("RIFF\0\0WAVE", 10)) != std::string::npos);
    CHECK(ai::ParseTranscript(c.provider, 200, R"({"text":"xin chào"})", t, e) && t == "xin chào");
    c.provider = ai::Provider::Gemini;
    r = ai::TranscribeRequest(c, "abc");
    CHECK(Has(r.body, "\"data\":\"YWJj\"") && Has(r.body, "audio/wav"));
    CHECK(ai::Base64("ab") == "YWI=" && ai::Base64("a") == "YQ==" && ai::Base64("") == "");
    CHECK(ai::MaskKey("sk-proj-abcdefgh1234") == "sk-…1234");
    DONE();
}
