#include "ai_client.h"

#include <cJSON.h>

#include <algorithm>

namespace ai {
namespace {

std::string File() { return platform::StateDir() + "/ai.json"; }

const ProviderInfo kProviders[] = {
    {Provider::OpenAI, "openai", "ChatGPT (OpenAI)", Wire::OpenAI, "https://api.openai.com/v1", "gpt-4o-mini", "gpt-4o-mini-transcribe", "platform.openai.com › API keys"},
    {Provider::Gemini, "gemini", "Gemini (Google)", Wire::Gemini, "", "gemini-2.5-flash", "inline", "aistudio.google.com › Get API key"},
    {Provider::Compatible, "compatible", "Máy chủ riêng (Ollama, LM Studio…)", Wire::OpenAI, "", "llama3.2", "", "không cần khoá nếu máy chủ không đòi"},
    {Provider::Claude, "claude", "Claude (Anthropic)", Wire::Anthropic, "https://api.anthropic.com/v1", "claude-opus-5-5", "", "platform.claude.com › API keys"},
    {Provider::DeepSeek, "deepseek", "DeepSeek", Wire::OpenAI, "https://api.deepseek.com/v1", "deepseek-chat", "", "platform.deepseek.com › API keys"},
    {Provider::Grok, "grok", "Grok (xAI)", Wire::OpenAI, "https://api.x.ai/v1", "grok-3-mini", "", "console.x.ai › API keys"},
    {Provider::Mistral, "mistral", "Mistral", Wire::OpenAI, "https://api.mistral.ai/v1", "mistral-small-latest", "", "console.mistral.ai › API keys"},
    {Provider::Groq, "groq", "Groq (Llama, siêu nhanh)", Wire::OpenAI, "https://api.groq.com/openai/v1", "llama-3.3-70b-versatile", "whisper-large-v3", "console.groq.com › API keys"},
    {Provider::OpenRouter, "openrouter", "OpenRouter (nhiều model)", Wire::OpenAI, "https://openrouter.ai/api/v1", "openrouter/auto", "", "openrouter.ai › Keys"},
};

std::string Str(const cJSON* o, const char* k) {
    const cJSON* v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}

std::string Trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\n')) ++a;
    return s.substr(a);
}

std::string OpenAiBase(const Config& c) {
    std::string b = c.provider == Provider::Compatible || !c.base_url.empty() ? c.base_url : Info(c.provider).base_url;
    while (!b.empty() && b.back() == '/') b.pop_back();
    return b;
}

std::string ApiError(const cJSON* root, int status) {
    const cJSON* e = cJSON_GetObjectItem(root, "error");
    std::string m = cJSON_IsObject(e) ? Str(e, "message") : cJSON_IsString(e) ? e->valuestring : "";
    if (status == 401 || status == 403) return "Khoá API không đúng hoặc chưa được cấp quyền" + (m.empty() ? "" : ": " + m);
    if (status == 429) return "Vượt giới hạn hoặc hết hạn mức của khoá API" + (m.empty() ? "" : ": " + m);
    if (!m.empty()) return m;
    return status ? "Lỗi HTTP " + std::to_string(status) : "Không kết nối được máy chủ AI";
}

}  // namespace

const ProviderInfo& Info(Provider p) {
    for (const auto& i : kProviders) if (i.id == p) return i;
    return kProviders[0];
}
const char* ProviderName(Provider p) { return Info(p).name; }
std::string DefaultModel(Provider p) { return Info(p).model; }

std::string MaskKey(const std::string& k) {
    if (k.empty()) return "chưa nhập";
    if (k.size() <= 8) return "••••";
    return k.substr(0, 3) + "…" + k.substr(k.size() - 4);
}

Config Load() {
    Config c;
    std::string s;
    if (!platform::ReadText(File(), s)) return c;
    cJSON* o = cJSON_Parse(s.c_str());
    if (cJSON_IsObject(o)) {
        std::string p = Str(o, "provider");
        for (const auto& i : kProviders) if (p == i.key) c.provider = i.id;
        c.api_key = Str(o, "api_key");
        c.model = Str(o, "model");
        c.base_url = Str(o, "base_url");
        c.transcribe_model = Str(o, "transcribe_model");
        const cJSON* v = cJSON_GetObjectItem(o, "voice");
        if (cJSON_IsBool(v)) c.voice = cJSON_IsTrue(v);
    }
    cJSON_Delete(o);
    return c;
}

void Save(const Config& c) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "provider", Info(c.provider).key);
    cJSON_AddStringToObject(o, "api_key", c.api_key.c_str());
    cJSON_AddStringToObject(o, "model", c.model.c_str());
    cJSON_AddStringToObject(o, "base_url", c.base_url.c_str());
    cJSON_AddStringToObject(o, "transcribe_model", c.transcribe_model.c_str());
    cJSON_AddBoolToObject(o, "voice", c.voice);
    char* out = cJSON_Print(o);
    if (out) platform::WriteText(File(), out);
    cJSON_free(out);
    cJSON_Delete(o);
}

std::string SystemPrompt(const Context& ctx) {
    std::string s =
        "Bạn là trợ lý đọc sách trên máy đọc sách e-ink Metalio. Trả lời bằng tiếng Việt (trừ khi người dùng "
        "hỏi bằng ngôn ngữ khác), ngắn gọn, rõ ràng, chữ thường không dùng Markdown, bảng hay biểu tượng cảm xúc "
        "vì màn hình đen trắng nhỏ. Tối đa khoảng 200 từ trừ khi được yêu cầu dài hơn.";
    if (!ctx.book_title.empty()) s += "\nNgười dùng đang đọc sách: \"" + ctx.book_title + "\"" + (ctx.chapter.empty() ? "" : ", chương \"" + ctx.chapter + "\"") + ".";
    if (!ctx.selection.empty()) s += "\nĐoạn người dùng đang chọn trong sách:\n\"\"\"\n" + ctx.selection.substr(0, 4000) + "\n\"\"\"";
    else if (!ctx.page_text.empty()) s += "\nNội dung trang đang mở:\n\"\"\"\n" + ctx.page_text.substr(0, 3000) + "\n\"\"\"";
    return s;
}

platform::HttpRequest ChatRequest(const Config& c, const std::vector<Message>& history, const Context& ctx) {
    platform::HttpRequest r;
    r.method = "POST";
    r.content_type = "application/json";
    r.timeout_ms = 60000;
    r.max_body = 256 * 1024;
    const std::string model = c.model.empty() ? DefaultModel(c.provider) : c.model;
    cJSON* root = cJSON_CreateObject();
    const Wire wire = Info(c.provider).wire;
    if (wire == Wire::Anthropic) {
        // Messages API (raw HTTP, anthropic-version 2026-10-21). Server-side fallback lets a request
        // the model declines be retried on a suitable model instead of failing.
        r.url = OpenAiBase(c) + "/messages";
        r.headers.push_back({"x-api-key", c.api_key});
        r.headers.push_back({"anthropic-version", "2026-10-21"});
        r.headers.push_back({"anthropic-beta", "server-side-fallback-2026-07-01"});
        r.timeout_ms = 120000;
        cJSON_AddStringToObject(root, "model", model.c_str());
        cJSON_AddNumberToObject(root, "max_tokens", 16000);
        cJSON_AddStringToObject(root, "system", SystemPrompt(ctx).c_str());
        cJSON_AddStringToObject(root, "fallbacks", "default");
        cJSON* msgs = cJSON_AddArrayToObject(root, "messages");
        for (const auto& m : history) {
            cJSON* msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "role", m.role == "assistant" ? "assistant" : "user");
            cJSON_AddStringToObject(msg, "content", m.text.c_str());
            cJSON_AddItemToArray(msgs, msg);
        }
    } else if (wire == Wire::Gemini) {
        r.url = "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent";
        r.headers.push_back({"x-goog-api-key", c.api_key});
        cJSON* sys = cJSON_AddObjectToObject(root, "system_instruction");
        cJSON* sp = cJSON_AddArrayToObject(sys, "parts");
        cJSON* t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "text", SystemPrompt(ctx).c_str());
        cJSON_AddItemToArray(sp, t);
        cJSON* contents = cJSON_AddArrayToObject(root, "contents");
        for (const auto& m : history) {
            cJSON* msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "role", m.role == "assistant" ? "model" : "user");
            cJSON* parts = cJSON_AddArrayToObject(msg, "parts");
            cJSON* pt = cJSON_CreateObject();
            cJSON_AddStringToObject(pt, "text", m.text.c_str());
            cJSON_AddItemToArray(parts, pt);
            cJSON_AddItemToArray(contents, msg);
        }
    } else {
        r.url = OpenAiBase(c) + "/chat/completions";
        if (!c.api_key.empty()) r.headers.push_back({"Authorization", "Bearer " + c.api_key});
        if (c.provider == Provider::OpenRouter) r.headers.push_back({"X-Title", "Metalio Reader"});
        cJSON_AddStringToObject(root, "model", model.c_str());
        cJSON* msgs = cJSON_AddArrayToObject(root, "messages");
        cJSON* sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", SystemPrompt(ctx).c_str());
        cJSON_AddItemToArray(msgs, sys);
        for (const auto& m : history) {
            cJSON* msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "role", m.role == "assistant" ? "assistant" : "user");
            cJSON_AddStringToObject(msg, "content", m.text.c_str());
            cJSON_AddItemToArray(msgs, msg);
        }
    }
    char* body = cJSON_PrintUnformatted(root);
    r.body = body ? body : "";
    cJSON_free(body);
    cJSON_Delete(root);
    return r;
}

bool ParseChat(Provider p, int status, const std::string& body, std::string& text, std::string& error) {
    cJSON* root = cJSON_Parse(body.c_str());
    bool ok = false;
    if (status >= 200 && status < 300 && root) {
        if (Info(p).wire == Wire::Anthropic) {
            // content[] holds thinking blocks (empty text by default) and text blocks: keep text only.
            const cJSON* it;
            std::string all;
            cJSON_ArrayForEach(it, cJSON_GetObjectItem(root, "content"))
                if (Str(it, "type") == "text") all += Str(it, "text");
            if (Str(root, "stop_reason") == "refusal") error = "Claude từ chối trả lời câu này.";
            else if (!all.empty()) { text = Trimmed(all); ok = true; }
            else error = "Claude không trả về nội dung.";
        } else if (Info(p).wire == Wire::Gemini) {
            const cJSON* cand = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "candidates"), 0);
            const cJSON* parts = cJSON_GetObjectItem(cJSON_GetObjectItem(cand, "content"), "parts");
            const cJSON* it;
            std::string all;
            cJSON_ArrayForEach(it, parts) all += Str(it, "text");
            if (!all.empty()) { text = Trimmed(all); ok = true; }
            else error = Str(cand, "finishReason") == "SAFETY" ? "Gemini từ chối trả lời câu này." : "Gemini không trả về nội dung.";
        } else {
            const cJSON* ch = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "choices"), 0);
            std::string t = Str(cJSON_GetObjectItem(ch, "message"), "content");
            if (!t.empty()) { text = Trimmed(t); ok = true; }
            else error = "Máy chủ không trả về nội dung.";
        }
    } else {
        error = ApiError(root, status);
    }
    cJSON_Delete(root);
    return ok;
}

std::string Base64(const std::string& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((d.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < d.size(); i += 3) {
        uint32_t v = ((uint8_t)d[i] << 16) | ((uint8_t)d[i + 1] << 8) | (uint8_t)d[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == d.size()) {
        uint32_t v = (uint8_t)d[i] << 16;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == d.size()) {
        uint32_t v = ((uint8_t)d[i] << 16) | ((uint8_t)d[i + 1] << 8);
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
    }
    return o;
}

platform::HttpRequest TranscribeRequest(const Config& c, const std::string& wav) {
    platform::HttpRequest r;
    r.method = "POST";
    r.timeout_ms = 60000;
    r.max_body = 64 * 1024;
    if (Info(c.provider).wire == Wire::Gemini) {
        const std::string model = c.model.empty() ? DefaultModel(c.provider) : c.model;
        r.url = "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent";
        r.headers.push_back({"x-goog-api-key", c.api_key});
        r.content_type = "application/json";
        r.body = std::string("{\"contents\":[{\"role\":\"user\",\"parts\":[") +
                 "{\"text\":\"Chép lại chính xác lời nói trong đoạn ghi âm (thường là tiếng Việt). Chỉ trả về lời nói, không thêm gì khác.\"}," +
                 "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"" + Base64(wav) + "\"}}]}]}";
        return r;
    }
    r.url = OpenAiBase(c) + "/audio/transcriptions";
    if (!c.api_key.empty()) r.headers.push_back({"Authorization", "Bearer " + c.api_key});
    const std::string boundary = "----metalio7e3f1c";
    r.content_type = "multipart/form-data; boundary=" + boundary;
    const std::string model = !c.transcribe_model.empty() ? c.transcribe_model : *Info(c.provider).stt_model ? Info(c.provider).stt_model : "whisper-1";
    auto field = [&](const std::string& name, const std::string& value) {
        return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
    };
    r.body = field("model", model) + field("language", "vi") +
             "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\nContent-Type: audio/wav\r\n\r\n" +
             wav + "\r\n--" + boundary + "--\r\n";
    return r;
}

bool ParseTranscript(Provider p, int status, const std::string& body, std::string& text, std::string& error) {
    if (Info(p).wire == Wire::Gemini) return ParseChat(p, status, body, text, error);
    cJSON* root = cJSON_Parse(body.c_str());
    bool ok = false;
    if (status >= 200 && status < 300 && root) {
        text = Trimmed(Str(root, "text"));
        ok = !text.empty();
        if (!ok) error = "Không nghe rõ, thử nói lại.";
    } else {
        error = ApiError(root, status);
    }
    cJSON_Delete(root);
    return ok;
}

}  // namespace ai
