// "Trợ lý AI": questions and answers with ChatGPT, Gemini, Claude… (Cài đặt › Trợ lý AI).
//   Ask by typing (Telex keyboard) or by voice: hold the AI key (or the mic button), talk, release.
//   Opened from a book it knows the title, chapter, page and the selected passage.
//   Free questions and answers by default; nothing is saved unless "Ghi chú" is switched on (then
//   the AI writes what is said / typed as a tidy note and saves it, with the passage as the quote).
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "ai_client.h"
#include "icons.h"
#include "notes.h"
#include "platform.h"
#include "screens.h"
#include "settings.h"
#include "ui_common.h"

namespace screens {
namespace {

const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);

struct Chat {
    ai::Config cfg;
    ai::Context ctx;
    std::string book_path;
    std::vector<ai::Message> history;
    uint32_t req = 0;
    bool busy = false, recording = false;
    bool note_mode = false;          // "Ghi chú": the AI turns what is said / typed into a saved note
    lv_obj_t* note_btn = nullptr;
    std::string status;
    lv_obj_t* list = nullptr;
    lv_obj_t* state = nullptr;
    lv_obj_t* mic = nullptr;
    std::shared_ptr<bool> alive;
};
Chat* g_chat = nullptr;              // the open assistant screen (for the AI key release)
std::vector<ai::Message> g_keep;     // conversation kept while the device is on
std::string g_keep_book;

std::string VoicePath() { return platform::StateDir() + "/ai-voice.wav"; }

void Render(Chat* c);

// Note mode wraps the request; the transcript shows the user's own words.
const char* kNoteAsk = "[GHI CHÚ] Viết lại nội dung sau thành một ghi chú ngắn gọn, rõ ràng bằng tiếng Việt. "
                       "Dòng đầu là tiêu đề ngắn, sau đó các ý chính. Chỉ trả về ghi chú.\n\n";

std::string Shown(const ai::Message& m) {
    const size_t n = std::strlen(kNoteAsk);
    if (m.role == "user" && m.text.compare(0, n, kNoteAsk) == 0) return std::string(icon::NOTES) + "  " + m.text.substr(n);
    return m.text;
}

void SaveNote(Chat* c, const std::string& text) {
    notes::Note n;
    n.book = c->book_path;
    n.book_title = c->ctx.book_title;
    n.chapter_title = c->ctx.chapter;
    n.quote = c->ctx.selection;
    n.text = text;
    notes::Add(n);
}


void SetState(Chat* c, const std::string& s) {
    c->status = s;
    if (c->state) lv_label_set_text(c->state, s.c_str());
}

void Ask(Chat* c, const std::string& q) {
    if (q.empty() || c->busy) return;
    if (!c->cfg.configured()) { OpenAssistantSettings(); return; }
    const bool note = c->note_mode;
    c->history.push_back({"user", note ? kNoteAsk + q : q});
    if (c->history.size() > 12) c->history.erase(c->history.begin(), c->history.begin() + 2);   // keep requests small
    c->busy = true;
    Render(c);
    SetState(c, "Đang hỏi " + std::string(ai::ProviderName(c->cfg.provider)) + "…");
    auto alive = c->alive;
    const auto provider = c->cfg.provider;
    c->req = platform::Http(ai::ChatRequest(c->cfg, c->history, c->ctx), [c, alive, provider, note](const platform::HttpResponse& r) {
        if (!*alive) return;
        c->req = 0;
        c->busy = false;
        std::string text, err;
        if (ai::ParseChat(provider, r.status, r.body, text, err)) {
            c->history.push_back({"assistant", text});
            if (note) { SaveNote(c, text); SetState(c, std::string(icon::CHECK) + "  Đã lưu vào Ghi chú"); }
            else SetState(c, "");
        } else {
            c->history.pop_back();   // let the question be asked again
            SetState(c, std::string(icon::INFO) + "  " + (r.status == 0 && !r.error.empty() ? "Không kết nối được: " + r.error : err));
        }
        g_keep = c->history;
        Render(c);
    });
}

void StartListening(Chat* c) {
    if (c->busy || c->recording) return;
    if (!c->cfg.configured()) { OpenAssistantSettings(); return; }
    if (!c->cfg.voice || !c->cfg.voice_supported()) {
        ui::Toast(std::string(ai::ProviderName(c->cfg.provider)) + " không nhận giọng nói. Hãy gõ câu hỏi, hoặc chọn ChatGPT / Gemini / Groq.", 2500);
        return;
    }
    if (!platform::RecStart(VoicePath())) { ui::Toast("Không bật được micro."); return; }
    c->recording = true;
    platform::Vibrate(settings::Get().haptic);
    SetState(c, std::string(icon::MIC) + "  Đang nghe… thả phím khi nói xong");
    Render(c);
}

void StopListening(Chat* c) {
    if (!c->recording) return;
    c->recording = false;
    platform::RecStop();
    std::string wav;
    if (!platform::ReadText(VoicePath(), wav) || wav.size() < 3200) {   // < 0.1 s
        SetState(c, "Chưa nghe thấy gì. Giữ phím AI trong lúc nói.");
        Render(c);
        return;
    }
    c->busy = true;
    SetState(c, "Đang chuyển giọng nói thành chữ…");
    Render(c);
    auto alive = c->alive;
    const auto provider = c->cfg.provider;
    c->req = platform::Http(ai::TranscribeRequest(c->cfg, wav), [c, alive, provider](const platform::HttpResponse& r) {
        if (!*alive) return;
        c->req = 0;
        c->busy = false;
        std::string text, err;
        platform::RemoveFile(VoicePath());
        if (ai::ParseTranscript(provider, r.status, r.body, text, err)) Ask(c, text);
        else { SetState(c, std::string(icon::INFO) + "  " + err); Render(c); }
    });
}

void TypeQuestion(Chat* c) {
    ui::PromptOptions o;
    o.title = c->note_mode ? "Ghi chú với AI" : "Hỏi trợ lý AI";
    o.hint = c->ctx.book_title.empty() ? "Hỏi bất cứ điều gì. Gõ Telex: aa=â, ow=ơ, dd=đ, s f r x j = dấu."
                                       : "Trợ lý biết bạn đang đọc \"" + c->ctx.book_title + "\"" + (c->ctx.selection.empty() ? "." : " và đoạn bạn chọn.");
    o.multiline = true;
    o.ok = "Hỏi";
    auto alive = c->alive;
    ui::TextPrompt(o, [c, alive](const std::string& q) { if (*alive) Ask(c, q); });
}

lv_obj_t* Plain(lv_obj_t* parent, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

void Render(Chat* c) {
    lv_obj_clean(c->list);
    // Context card
    if (!c->ctx.book_title.empty()) {
        lv_obj_t* card = lv_obj_create(c->list);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, kHair, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, 14, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = ui::Label(card, std::string(icon::BOOK) + "  " + c->ctx.book_title + (c->ctx.chapter.empty() ? "" : " · " + c->ctx.chapter), ui::F().small);
        lv_obj_set_size(t, LV_PCT(100), lv_font_get_line_height(ui::F().small));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        if (!c->ctx.selection.empty()) {
            lv_obj_t* q = ui::Label(card, "“" + c->ctx.selection + "”", ui::F().serif_italic);
            lv_obj_set_size(q, LV_PCT(100), lv_font_get_line_height(ui::F().serif_italic) * 2 + 4);
            lv_label_set_long_mode(q, LV_LABEL_LONG_MODE_DOTS);
        }
    }
    if (c->history.empty() && !c->busy && !c->recording) {
        lv_obj_t* e = ui::Label(c->list, c->cfg.configured()
            ? (c->note_mode ? "Chế độ ghi chú: nói hoặc gõ, AI viết thành ghi chú gọn và lưu vào Ghi chú."
                            : "Hỏi gì cũng được. Giữ phím AI để nói, hoặc bấm \"Gõ câu hỏi\".")
            : "Chưa cài trợ lý. Bấm nút cài đặt ở góc trên để chọn ChatGPT, Gemini, Claude… và nhập khoá API.", ui::F().serif_italic);
        lv_obj_set_width(e, LV_PCT(100));
        lv_label_set_long_mode(e, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_color(e, kMuted, 0);
        // Suggested questions
        std::vector<std::string> sug;   // only when opened from a book: free questions need no prompts
        if (c->cfg.configured() && !c->note_mode) {
            if (!c->ctx.selection.empty()) sug = {"Giải thích đoạn này", "Dịch sang tiếng Việt", "Các từ khó trong đoạn"};
            else if (!c->ctx.book_title.empty()) sug = {"Tóm tắt trang này", "Gợi ý sách tương tự"};
        }
        lv_obj_t* chips = Plain(c->list, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_gap(chips, 10, 0);
        for (const auto& q : sug) {
            lv_obj_t* b = ui::Button(chips, q, [c, q] { Ask(c, q); }, LV_SIZE_CONTENT, 64);
            lv_obj_set_style_radius(b, 32, 0);
            lv_obj_set_style_border_width(b, 1, 0);
        }
    }
    for (size_t i = 0; i < c->history.size(); ++i) {
        const auto& m = c->history[i];
        const bool user = m.role == "user";
        lv_obj_t* box = lv_obj_create(c->list);
        lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_radius(box, 12, 0);
        lv_obj_set_style_pad_all(box, user ? 12 : 16, 0);
        lv_obj_set_style_border_width(box, user ? 0 : 2, 0);
        lv_obj_set_style_bg_color(box, user ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, 10, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = ui::Label(box, Shown(m), user ? ui::F().body_bold : ui::F().reader(30, false));
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_line_space(t, 6, 0);
        if (user) lv_obj_set_style_text_color(t, lv_color_white(), 0);
    }
    if (!c->status.empty()) {
        c->state = ui::Label(c->list, c->status, ui::F().body);
        lv_obj_set_width(c->state, LV_PCT(100));
        lv_label_set_long_mode(c->state, LV_LABEL_LONG_MODE_WRAP);
    } else {
        c->state = nullptr;
    }
    lv_obj_update_layout(c->list);
    lv_obj_scroll_to_y(c->list, LV_COORD_MAX, LV_ANIM_OFF);   // newest answer in view
    if (c->mic) {
        lv_label_set_text(lv_obj_get_child(c->mic, 0), c->recording ? (std::string(icon::STOP) + "  Thả để gửi").c_str()
                                                                     : (std::string(icon::MIC) + "  Giữ để nói").c_str());
        ui::Invert(c->mic, c->recording);
    }
    if (c->note_btn) ui::Invert(c->note_btn, c->note_mode);
}

}  // namespace

void AssistantKeyUp() {
    if (g_chat) StopListening(g_chat);
}

void OpenAssistant(const std::string& selection, const std::string& question, bool listen) {
    // AI key held while the assistant is already open: talk in the same conversation.
    if (g_chat && lv_screen_active() == lv_obj_get_screen(g_chat->list) && selection.empty() && question.empty()) {
        if (listen) StartListening(g_chat);
        return;
    }
    auto* c = new Chat();
    c->cfg = ai::Load();
    ui::ReadingContext rc;
    if (ui::CurrentReadingContext(rc)) {
        c->book_path = rc.book_path;
        c->ctx.book_title = rc.title;
        c->ctx.chapter = rc.chapter;
        c->ctx.page_text = rc.page_text;
    }
    c->ctx.selection = selection;
    if (c->book_path == g_keep_book) c->history = g_keep;   // same book (or none): continue the chat
    else g_keep.clear();
    g_keep_book = c->book_path;
    const std::string back_to = c->book_path;
    ui::Page p = ui::NewPage("TRỢ LÝ AI", [back_to] { if (!back_to.empty()) OpenReader(back_to); else OpenApps(); });
    c->alive = ui::Alive(p.screen);
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) {
        auto* x = static_cast<Chat*>(lv_event_get_user_data(e));
        if (x->recording) platform::RecStop();
        if (x->req) platform::Cancel(x->req);
        if (g_chat == x) g_chat = nullptr;
        delete x;
    }, LV_EVENT_DELETE, c);
    g_chat = c;
    lv_label_set_text(p.header_status, c->cfg.configured() ? ai::ProviderName(c->cfg.provider) : "Chưa cài đặt");
    lv_obj_remove_flag(p.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(p.body, 10, 0);
    c->list = lv_obj_create(p.body);
    lv_obj_set_width(c->list, LV_PCT(100));
    lv_obj_set_flex_grow(c->list, 1);
    lv_obj_set_style_border_width(c->list, 0, 0);
    lv_obj_set_style_pad_all(c->list, 0, 0);
    lv_obj_set_style_pad_row(c->list, 14, 0);
    lv_obj_set_flex_flow(c->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(c->list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(c->list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    lv_obj_t* bar = Plain(p.body, LV_PCT(100), 84);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 10, 0);
    const int bw = (ui::kW - 2 * ui::kPad - 20) / 3;
    auto label = [](lv_obj_t* b, const std::string& t) {   // regular weight keeps the text off the pill edge
        lv_obj_t* l = ui::Label(b, t, ui::F().body);
        lv_obj_center(l);
        lv_obj_set_style_pad_hor(b, 8, 0);
        return l;
    };
    c->mic = ui::Button(bar, "", [] {}, bw, 80);   // push-to-talk: PRESSED starts, RELEASED sends
    label(c->mic, "");
    lv_obj_set_style_radius(c->mic, 40, 0);
    lv_obj_add_event_cb(c->mic, [](lv_event_t* e) { StartListening(static_cast<Chat*>(lv_event_get_user_data(e))); }, LV_EVENT_PRESSED, c);
    lv_obj_add_event_cb(c->mic, [](lv_event_t* e) { StopListening(static_cast<Chat*>(lv_event_get_user_data(e))); }, LV_EVENT_RELEASED, c);
    lv_obj_t* type = ui::Button(bar, "", [c] { TypeQuestion(c); }, bw, 80);
    label(type, std::string(icon::KEYBOARD) + "  Gõ câu hỏi");
    lv_obj_set_style_radius(type, 40, 0);
    c->note_btn = ui::Button(bar, "", [c] {
        c->note_mode = !c->note_mode;
        ui::Toast(c->note_mode ? "Bật ghi chú: AI sẽ viết thành ghi chú và lưu lại." : "Tắt ghi chú: hỏi đáp tự do, không lưu.", 1800);
        Render(c);
    }, bw, 80);
    label(c->note_btn, std::string(icon::NOTES) + "  Ghi chú");
    lv_obj_set_style_radius(c->note_btn, 40, 0);
    lv_obj_set_style_border_width(c->note_btn, 2, 0);
    // All API settings live behind one gear button in the header.
    lv_obj_t* gear = ui::Button(lv_obj_get_parent(p.header_status), icon::SETTINGS, [] { OpenAssistantSettings(); }, 76, 76);
    lv_obj_set_style_text_font(gear, ui::IconFont(36), 0);
    lv_obj_set_style_border_width(gear, 1, 0);
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(p.header_status, ui::kW - 2 * ui::kPad - 184 - 90);

    // Page keys scroll long answers.
    lv_obj_t* list = c->list;
    ui::SetPageKeyHandler([list](int dir) { lv_obj_scroll_by_bounded(list, 0, -dir * (lv_obj_get_height(list) - 80), LV_ANIM_OFF); });
    Render(c);
    ui::Show(p.screen);
    if (!question.empty()) Ask(c, question);
    else if (listen) StartListening(c);
}

}  // namespace screens
