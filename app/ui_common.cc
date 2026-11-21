#include "icons.h"
#include "ui_common.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "platform.h"
#include "settings.h"
#include "telex.h"
#include "src/libs/tjpgd/tjpgd.h"  // TJpgDec bundled with LVGL (device: JPEGDEC)
#include <memory>

namespace ui {
namespace {

Fonts g_fonts;
Action g_back;
std::function<void(int)> g_page;
std::function<void(Tab)> g_nav_open = [](Tab) {};
lv_obj_t* g_toast = nullptr;
lv_timer_t* g_toast_timer = nullptr;

// Owns a std::function attached to an LVGL object; freed on LV_EVENT_DELETE.
void AttachAction(lv_obj_t* obj, lv_event_code_t code, Action a) {
    auto* fn = new Action(std::move(a));
    // Run a copy: the action often rebuilds the screen and deletes this very object (and the
    // stored closure) while it is still running.
    lv_obj_add_event_cb(
        obj, [](lv_event_t* e) { Action copy = *static_cast<Action*>(lv_event_get_user_data(e)); copy(); }, code, fn);
    lv_obj_add_event_cb(
        obj, [](lv_event_t* e) { delete static_cast<Action*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, fn);
}

void StyleFocus(lv_obj_t* obj) {
    // Keypad focus must be obvious on e-ink: thick outline, no animation.
    lv_obj_set_style_outline_width(obj, 5, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(obj, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(obj, 3, LV_STATE_FOCUS_KEY);
}

struct GreyImage {
    lv_image_dsc_t dsc{};
    std::vector<uint8_t> pixels;
};

struct JpegIn {
    const std::vector<uint8_t>* data;
    size_t pos;
    std::vector<uint8_t> grey;  // full-size luminance
    int width;
};

size_t JpegRead(JDEC* jd, uint8_t* buf, size_t n) {
    auto* in = static_cast<JpegIn*>(jd->device);
    n = std::min(n, in->data->size() - in->pos);
    if (buf) std::memcpy(buf, in->data->data() + in->pos, n);
    in->pos += n;
    return n;
}

int JpegWrite(JDEC* jd, void* bitmap, JRECT* r) {
    auto* in = static_cast<JpegIn*>(jd->device);
    const uint8_t* px = static_cast<const uint8_t*>(bitmap);  // RGB888 (JD_FORMAT 0)
    for (int y = r->top; y <= r->bottom; ++y)
        for (int x = r->left; x <= r->right; ++x, px += 3)
            in->grey[(size_t)y * in->width + x] = (uint8_t)((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
    return 1;
}

// Baseline JPEG -> w*h grey, scaled to cover the frame and centre-cropped (box filter).
bool DecodeCoverJpeg(const std::vector<uint8_t>& data, int w, int h, std::vector<uint8_t>& out) {
    JpegIn in{&data, 0, {}, 0};
    std::vector<uint8_t> pool(8192);
    JDEC jd;
    if (jd_prepare(&jd, JpegRead, pool.data(), pool.size(), &in) != JDR_OK) return false;  // progressive, etc.
    if (jd.width == 0 || jd.height == 0 || (size_t)jd.width * jd.height > 16u * 1024 * 1024) return false;
    in.width = jd.width;
    in.grey.assign((size_t)jd.width * jd.height, 255);
    if (jd_decomp(&jd, JpegWrite, 0) != JDR_OK) return false;
    const double scale = std::max((double)w / jd.width, (double)h / jd.height);
    const double ox = (jd.width * scale - w) / 2, oy = (jd.height * scale - h) / 2;
    out.assign((size_t)w * h, 255);
    for (int y = 0; y < h; ++y) {
        int sy0 = (int)((y + oy) / scale), sy1 = std::max(sy0 + 1, (int)((y + 1 + oy) / scale));
        for (int x = 0; x < w; ++x) {
            int sx0 = (int)((x + ox) / scale), sx1 = std::max(sx0 + 1, (int)((x + 1 + ox) / scale));
            unsigned sum = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < jd.height; ++sy)
                for (int sx = sx0; sx < sx1 && sx < jd.width; ++sx) { sum += in.grey[(size_t)sy * jd.width + sx]; ++n; }
            out[(size_t)y * w + x] = n ? (uint8_t)(sum / n) : 255;
        }
    }
    return true;
}

}  // namespace

void SetFonts(const Fonts& f) { g_fonts = f; }

namespace { Fonts g_default_fonts; bool g_have_default = false; }

void ApplyUiFont(const std::string& family) {
    if (!g_have_default) { g_default_fonts = g_fonts; g_have_default = true; }
    Fonts f = g_default_fonts;
    if (family != "sans" && g_fonts.face) {
        auto get = [&](int px, bool bold, const lv_font_t* def) {
            const lv_font_t* r = g_fonts.face(family, px, bold);
            return r ? r : def;
        };
        f.small = get(25, false, f.small);
        f.body = get(28, false, f.body);
        f.body_bold = get(28, true, f.body_bold);
        f.title = get(36, true, f.title);
        f.big = get(52, true, f.big);
        if (family.rfind("file:", 0) == 0) {   // a custom face replaces the serif accents too
            f.serif_title = get(40, true, f.serif_title);
            f.serif_book = get(30, true, f.serif_book);
            f.serif_italic = get(24, false, f.serif_italic);
        }
    }
    g_fonts = f;
    lv_display_t* d = lv_display_get_default();
    if (d) lv_display_set_theme(d, lv_theme_mono_init(d, false, f.body));
}

std::string FontDisplayName(const std::string& family) {
    if (family == "sans") return "Không chân · Noto Sans";
    if (family == "serif") return "Có chân · Noto Serif";
    if (family.rfind("file:", 0) == 0) {
        std::string n = family.substr(5);
        size_t dot = n.rfind('.');
        return dot == std::string::npos ? n : n.substr(0, dot);
    }
    return family;
}

const lv_font_t* IconFont(int px) {
    const lv_font_t* f = g_fonts.icon_px ? g_fonts.icon_px(px) : nullptr;
    return f ? f : g_fonts.icon;
}

int ScreenW() { return (int)lv_display_get_horizontal_resolution(lv_display_get_default()); }
int ScreenH() { return (int)lv_display_get_vertical_resolution(lv_display_get_default()); }

namespace {
std::vector<lv_obj_t*> g_gray;  // device: EpdSetContentRole(obj, Cover/ReaderAntialias/...)
}

void MarkGray(lv_obj_t* obj, GrayRole) {
    g_gray.push_back(obj);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) {
        lv_obj_t* o = static_cast<lv_obj_t*>(lv_event_get_target(e));
        g_gray.erase(std::remove(g_gray.begin(), g_gray.end(), o), g_gray.end());
    }, LV_EVENT_DELETE, nullptr);
}

bool GrayContentVisible() {
    lv_obj_t* active = lv_screen_active();
    for (lv_obj_t* o : g_gray)
        if ((lv_obj_get_screen(o) == active || lv_obj_get_screen(o) == lv_layer_top()) && lv_obj_is_visible(o)) return true;
    return false;
}

std::shared_ptr<bool> Alive(lv_obj_t* screen) {
    auto* alive = new std::shared_ptr<bool>(std::make_shared<bool>(true));
    std::shared_ptr<bool> out = *alive;
    lv_obj_add_event_cb(
        screen,
        [](lv_event_t* e) {
            auto* a = static_cast<std::shared_ptr<bool>*>(lv_event_get_user_data(e));
            **a = false;
            delete a;
        },
        LV_EVENT_DELETE, alive);
    return out;
}
const Fonts& F() { return g_fonts; }

void SetBackHandler(Action a) { g_back = std::move(a); }
void SetPageKeyHandler(std::function<void(int)> fn) { g_page = std::move(fn); }
namespace {
uint32_t g_last_activity = 0;
Action g_ai_menu;
}
void SetAiMenuHandler(Action a) { g_ai_menu = std::move(a); }
namespace { Action g_bookmark; std::function<ReadingContext()> g_reading_ctx; }
void SetReadingContextProvider(std::function<ReadingContext()> fn) { g_reading_ctx = std::move(fn); }
bool CurrentReadingContext(ReadingContext& out) {
    if (!g_reading_ctx || ReaderPath().empty()) return false;
    out = g_reading_ctx();
    return true;
}
void SetBookmarkHandler(Action a) { g_bookmark = std::move(a); }
bool InvokeBookmark() {
    if (!g_bookmark || ReaderPath().empty()) return false;
    Action a = g_bookmark;
    a();
    return true;
}
bool InvokeAiMenu() {
    if (!g_ai_menu) return false;
    Action a = g_ai_menu;
    a();
    return true;
}
void NoteActivity() { g_last_activity = lv_tick_get(); }
uint32_t IdleMs() { return lv_tick_elaps(g_last_activity); }

bool InvokePageKey(int dir) {
    if (settings::Get().reverse_page_keys) dir = -dir;
    if (!g_page) return false;
    auto fn = g_page;
    fn(dir);
    return true;
}

void ReserveBottomNav(Page& p) { lv_obj_set_height(p.body, kH - 132 - kNavH); }

void BottomNav(lv_obj_t* screen, Tab active) {
    lv_obj_t* bar = lv_obj_create(screen);
    lv_obj_set_size(bar, kW, kNavH);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0xb9b6ae), 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    struct Item { Tab tab; const char* symbol; const char* text; };
    static const Item items[] = {{Tab::Home, icon::SHELF, "Kệ sách"},
                                 {Tab::Discover, icon::DISCOVER, "Khám phá"},
                                 {Tab::Assistant, icon::SPARKLES, "Trợ lý AI"},
                                 {Tab::Apps, icon::APPS, "Ứng dụng"}};
    const bool ai = settings::Get().ai_enabled;
    for (const auto& it : items) {
        Tab tab = it.tab;
        if (tab == Tab::Assistant && !ai) continue;   // Cài đặt › Trợ lý AI › tắt
        lv_obj_t* b = Button(bar, "", [tab] { g_nav_open(tab); }, ai ? 156 : 196, 86);
        lv_obj_set_style_border_width(b, 0, 0);
        const bool on = tab == active;
        const lv_color_t c = on ? lv_color_hex(0x1a1a18) : lv_color_hex(0x7a7872);
        lv_obj_t* ic = IconLabel(b, it.symbol);
        lv_obj_set_style_text_color(ic, c, 0);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_t* tx = Label(b, it.text, on ? g_fonts.body_bold : g_fonts.small);
        lv_obj_set_style_text_color(tx, c, 0);
        lv_obj_align(tx, LV_ALIGN_BOTTOM_MID, 0, -2);
        if (tab == active) {  // Kobo marks the current tab with a bar, not inversion
            lv_obj_t* mark = lv_obj_create(b);
            lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(mark, 44, 4);
            lv_obj_set_style_bg_color(mark, lv_color_black(), 0);
            lv_obj_set_style_border_width(mark, 0, 0);
            lv_obj_set_style_radius(mark, 2, 0);
            lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, -10);
        }
    }
}

void SetNavHandler(std::function<void(Tab)> fn) { g_nav_open = std::move(fn); }

lv_obj_t* IconLabel(lv_obj_t* parent, const char* symbol, const lv_font_t* font) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_font(l, font ? font : g_fonts.icon, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    return l;
}

std::string FormatTime(uint64_t ms) {
    uint64_t s = ms / 1000;
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    else std::snprintf(b, sizeof b, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    return b;
}

lv_obj_t* Progress(lv_obj_t* parent, int percent, int w, int h) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_remove_flag(bar, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(bar, w, h);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_radius(bar, h / 2, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_color(bar, lv_color_black(), 0);
    lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
    lv_obj_t* fill = lv_obj_create(bar);
    lv_obj_remove_flag(fill, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_obj_set_size(fill, LV_PCT(percent), LV_PCT(100));
    lv_obj_set_style_radius(fill, h / 2, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_bg_color(fill, lv_color_black(), 0);
    if (percent == 0) lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
    return bar;
}

lv_obj_t* Cover(lv_obj_t* parent, const std::vector<uint8_t>& image, const std::string& title,
                const std::string& author, int w, int h) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_flag(box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, 3, 0);                     // books have nearly square corners
    lv_obj_set_style_border_width(box, 1, 0);               // hairline edge for pale covers
    lv_obj_set_style_border_color(box, lv_color_hex(0x9a9a96), 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    if (!image.empty()) {
        // Decode once into an 8-bit grey bitmap of exactly the frame size: crisp on e-ink,
        // w*h bytes of RAM, and no scaling work at draw time.
        auto* o = new GreyImage{};
        if (DecodeCoverJpeg(image, w, h, o->pixels)) {
            o->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            o->dsc.header.cf = LV_COLOR_FORMAT_L8;
            o->dsc.header.w = (uint32_t)w;
            o->dsc.header.h = (uint32_t)h;
            o->dsc.header.stride = (uint32_t)w;
            o->dsc.data = o->pixels.data();
            o->dsc.data_size = (uint32_t)o->pixels.size();
            lv_obj_t* img = lv_image_create(box);
            lv_image_set_src(img, &o->dsc);
            lv_obj_center(img);
            MarkGray(img, GrayRole::Cover);
            lv_obj_add_event_cb(img, [](lv_event_t* e) { delete static_cast<GreyImage*>(lv_event_get_user_data(e)); },
                                LV_EVENT_DELETE, o);
            return box;
        }
        delete o;  // progressive JPEG, PNG, WebP, … → typographic cover
    }
    // Typographic "cloth-bound" cover: dark board, hairline frame, serif title in white.
    const bool small = w < 200;
    lv_obj_set_style_bg_color(box, lv_color_hex(0x3a3a38), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 3, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    const int inset = small ? 8 : 16;
    lv_obj_t* frame = lv_obj_create(box);
    lv_obj_remove_flag(frame, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(frame, w - 2 * inset, h - 2 * inset);
    lv_obj_center(frame);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xd8d4c8), 0);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_set_style_pad_all(frame, small ? 8 : 18, 0);
    const lv_font_t* tf = g_fonts.reader ? g_fonts.reader(small ? 20 : 30, true) : g_fonts.body_bold;
    lv_obj_t* t = Label(frame, title, tf);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(t, small ? 0 : 4, 0);
    lv_obj_set_width(t, LV_PCT(100));
    lv_obj_set_height(t, lv_font_get_line_height(tf) * (small ? 4 : 5));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, small ? 6 : 24);
    lv_obj_t* rule = lv_obj_create(frame);
    lv_obj_remove_flag(rule, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(rule, small ? 24 : 40, 1);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0xd8d4c8), 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_align(rule, LV_ALIGN_BOTTOM_MID, 0, small ? -30 : -46);
    std::string au = author.empty() ? "Metalio" : author;
    const lv_font_t* af = g_fonts.reader ? g_fonts.reader(small ? 16 : 22, false) : g_fonts.small;
    lv_obj_t* a = Label(frame, au, af);
    lv_obj_set_style_text_color(a, lv_color_hex(0xe8e4d8), 0);
    lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(a, LV_PCT(100));
    lv_label_set_long_mode(a, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(a, lv_font_get_line_height(af));
    lv_obj_align(a, LV_ALIGN_BOTTOM_MID, 0, small ? 0 : -6);
    return box;
}
void InvokeBack() {
    if (g_back) {
        Action a = g_back;  // handler may replace itself
        a();
    }
}

lv_obj_t* Label(lv_obj_t* parent, const std::string& text, const lv_font_t* font) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_font(l, font ? font : g_fonts.body, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    return l;
}

lv_obj_t* Button(lv_obj_t* parent, const std::string& text, Action on_click, int w, int h) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_black(), 0);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_bg_color(b, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_hex(0xa8a6a0), LV_STATE_DISABLED);   // greyed, not hidden
    lv_obj_set_style_border_color(b, lv_color_hex(0xc8c6c0), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, lv_color_black(), 0);
    StyleFocus(b);
    if (!text.empty()) {
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, text.c_str());
        lv_obj_set_style_text_font(l, g_fonts.body_bold, 0);
        lv_obj_center(l);
    }
    if (on_click) AttachAction(b, LV_EVENT_CLICKED, std::move(on_click));
    return b;
}

void Invert(lv_obj_t* obj, bool black) {
    lv_obj_set_style_bg_color(obj, black ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_text_color(obj, black ? lv_color_white() : lv_color_black(), 0);
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* c = lv_obj_get_child(obj, i);
        lv_obj_set_style_text_color(c, black ? lv_color_white() : lv_color_black(), 0);
    }
}

Page NewPage(const char* title, Action on_back) {
    PrepareOrientation(false);
    Page p;
    p.screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(p.screen, lv_color_white(), 0);
    lv_obj_set_style_pad_all(p.screen, 0, 0);
    lv_obj_remove_flag(p.screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* head = lv_obj_create(p.screen);
    lv_obj_set_size(head, kW, 132);
    lv_obj_set_pos(head, 0, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(head, 4, 0);
    lv_obj_set_style_border_color(head, lv_color_black(), 0);
    lv_obj_set_style_pad_all(head, kPad, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    if (on_back) {
        lv_obj_t* back = Button(head, std::string(icon::CHEV_L) + " Trở về", [] { InvokeBack(); }, 164, 76);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_t* t = Label(head, title, g_fonts.title);
    lv_obj_set_size(t, kW - 2 * kPad - (on_back ? 184 : 0), lv_font_get_line_height(g_fonts.title));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, on_back ? 184 : 0, -4);
    p.header_status = Label(head, "", g_fonts.small);
    lv_obj_set_size(p.header_status, kW - 2 * kPad - (on_back ? 184 : 0), lv_font_get_line_height(g_fonts.small));
    lv_label_set_long_mode(p.header_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(p.header_status, LV_ALIGN_BOTTOM_LEFT, on_back ? 184 : 0, 4);
    // A page without a status line gets its title centred on the back button instead of hanging
    // at the top (checked when the screen loads, after the page filled in the status).
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) {
        auto* title = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
        lv_obj_t* status = lv_obj_get_child(lv_obj_get_parent(title), lv_obj_get_index(title) + 1);
        const char* txt = status ? lv_label_get_text(status) : "";
        const int x = lv_obj_get_index(title) > 0 ? 184 : 0;   // right of the back button when there is one
        if (!txt || !*txt) lv_obj_align(title, LV_ALIGN_LEFT_MID, x, 0);
    }, LV_EVENT_SCREEN_LOAD_START, t);

    p.body = lv_obj_create(p.screen);
    lv_obj_set_size(p.body, kW, kH - 132);
    lv_obj_set_pos(p.body, 0, 132);
    lv_obj_set_style_radius(p.body, 0, 0);
    lv_obj_set_style_border_width(p.body, 0, 0);
    lv_obj_set_style_pad_all(p.body, kPad, 0);
    lv_obj_set_style_pad_row(p.body, 16, 0);
    lv_obj_set_flex_flow(p.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(p.body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(p.body, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(p.body, LV_OBJ_FLAG_SCROLL_MOMENTUM);  // e-ink: no inertial scrolling
    SetBackHandler(std::move(on_back));
    SetPageKeyHandler(nullptr);
    SetAiMenuHandler(nullptr);
    return p;
}

namespace {
std::string g_reader_pending, g_reader_path;
}
void SetReaderOpen(const std::string& path) { g_reader_pending = path; }

void PrepareOrientation(bool reading) {
    int want = settings::Get().orient;
    if (!reading && (want == 1 || want == 2)) want = 0;   // landscape is a reading mode only
    if (platform::DisplayOrientation() != want) platform::SetDisplayOrientation(want);
}

namespace {
Action g_quick;
bool g_gesture_down = false, g_gesture_fired = false;
int g_gesture_y0 = 0;
}
void SetQuickSettingsHandler(Action open) { g_quick = std::move(open); }

bool FeedPointer(bool pressed, int x, int y) {
    (void)x;
    if (!pressed) {
        bool owned = g_gesture_fired;
        g_gesture_down = g_gesture_fired = false;
        return owned;
    }
    if (!g_gesture_down) {
        g_gesture_down = true;
        g_gesture_fired = false;
        g_gesture_y0 = y;
    }
    if (g_gesture_fired) return true;
    // Swipe starting on the top edge and pulled down far enough.
    if (g_gesture_y0 < 48 && y - g_gesture_y0 > 140 && g_quick) {
        g_gesture_fired = true;
        Action a = g_quick;
        a();
        return true;
    }
    return false;
}

Action GetBackHandler() { return g_back; }
const std::string& ReaderPath() { return g_reader_path; }

void Show(lv_obj_t* screen) {
    g_reader_path = g_reader_pending;   // empty unless the reader called SetReaderOpen() just before
    g_reader_pending.clear();
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_freeze(g, false);
    if (g_toast) lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);  // a toast belongs to the screen it came from
    // Deferred: Show() is usually called from a click handler of an object on the old screen,
    // which must not be deleted while its own event is still being dispatched.
    lv_async_call([](void* scr) {
        lv_obj_t* old = lv_screen_active();
        lv_obj_t* now = static_cast<lv_obj_t*>(scr);
        lv_screen_load(now);
        if (old && old != now) lv_obj_delete(old);
        // Deleting the old screen moves key focus to some object of the new one (often the last
        // button), and scroll-on-focus would then open a long page half scrolled. Focus the first
        // object of the new screen instead and start every page at the top.
        if (lv_group_t* g = lv_group_get_default()) {
            for (uint32_t i = 0; i < lv_group_get_obj_count(g); ++i) {
                lv_obj_t* o = lv_group_get_obj_by_index(g, i);
                if (lv_obj_get_screen(o) == now) { lv_group_focus_obj(o); break; }
            }
        }
        for (uint32_t i = 0; i < lv_obj_get_child_count(now); ++i) lv_obj_scroll_to_y(lv_obj_get_child(now, (int32_t)i), 0, LV_ANIM_OFF);
    }, screen);
}

void Toast(const std::string& text, uint32_t ms) {
    if (!g_toast) {
        g_toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(g_toast, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(g_toast, lv_color_white(), 0);
        lv_obj_set_style_text_font(g_toast, g_fonts.body, 0);
        lv_obj_set_style_pad_all(g_toast, 18, 0);
        lv_obj_set_style_radius(g_toast, 10, 0);
        lv_obj_set_width(g_toast, kW - 2 * kPad);
        lv_label_set_long_mode(g_toast, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_align(g_toast, LV_ALIGN_BOTTOM_MID, 0, -kPad);
    }
    lv_label_set_text(g_toast, text.c_str());
    lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
    if (g_toast_timer) lv_timer_delete(g_toast_timer);
    g_toast_timer = lv_timer_create(
        [](lv_timer_t*) {
            if (g_toast) lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
            lv_timer_delete(g_toast_timer);
            g_toast_timer = nullptr;
        },
        ms, nullptr);
}

}  // namespace ui

namespace ui {
namespace {

struct Prompt {
    lv_obj_t* ov = nullptr;
    lv_obj_t* ta = nullptr;
    lv_obj_t* mode = nullptr;
    bool vn = true, busy = false;
    std::string prev;
    std::function<void(const std::string&)> done;
    Action prev_back;
    lv_obj_t* prev_screen = nullptr;
};
Prompt* g_prompt = nullptr;

size_t ByteOfChar(const std::string& s, uint32_t chars) {
    size_t i = 0;
    while (chars > 0 && i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        --chars;
    }
    return std::min(i, s.size());
}
uint32_t CharsIn(const std::string& s) {
    uint32_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

void ClosePrompt(bool accept) {
    Prompt* p = g_prompt;
    if (!p) return;
    g_prompt = nullptr;
    std::string text = lv_textarea_get_text(p->ta);
    auto done = std::move(p->done);
    if (lv_screen_active() == p->prev_screen) SetBackHandler(p->prev_back);
    lv_obj_delete_async(p->ov);
    delete p;
    if (accept && done) done(text);
}

void RenderMode(Prompt* p) {
    lv_label_set_text(lv_obj_get_child(p->mode, 0), p->vn ? "Tiếng Việt (Telex)" : "English");
    Invert(p->mode, p->vn);
}

}  // namespace

lv_obj_t* StyledKeyboard(lv_obj_t* parent, lv_obj_t* ta, int h) {
    const lv_color_t ink = lv_color_hex(0x1a1a18);
    lv_obj_t* kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, LV_PCT(100), h);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_28, 0);   // ASCII keys + LV_SYMBOL_* (backspace, OK, …)
    lv_obj_set_style_bg_color(kb, lv_color_white(), 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, ink, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, ink, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, ink, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_white(), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    return kb;
}

bool TextPromptOpen() { return g_prompt != nullptr; }

void TextPrompt(const PromptOptions& o, std::function<void(const std::string&)> on_done) {
    if (g_prompt) ClosePrompt(false);
    auto* p = new Prompt();
    g_prompt = p;
    p->vn = o.vietnamese;
    p->done = std::move(on_done);
    p->prev_back = GetBackHandler();
    p->prev_screen = lv_screen_active();
    p->ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(p->ov, ScreenW(), ScreenH());
    lv_obj_set_pos(p->ov, 0, 0);
    lv_obj_set_style_radius(p->ov, 0, 0);
    lv_obj_set_style_border_width(p->ov, 0, 0);
    lv_obj_set_style_bg_color(p->ov, lv_color_white(), 0);
    lv_obj_set_style_pad_all(p->ov, kPad, 0);
    lv_obj_set_style_pad_row(p->ov, 12, 0);
    lv_obj_set_flex_flow(p->ov, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(p->ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p->ov, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* top = lv_obj_create(p->ov);
    lv_obj_set_size(top, LV_PCT(100), 76);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(Button(top, "Huỷ", [] { ClosePrompt(false); }, 120, 70), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* t = Label(top, o.title, g_fonts.title);
    lv_obj_set_width(t, ScreenW() - 2 * kPad - 300);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 140, 0);
    lv_obj_t* ok = Button(top, o.ok, [] { ClosePrompt(true); }, 140, 70);
    Invert(ok, true);
    lv_obj_align(ok, LV_ALIGN_RIGHT_MID, 0, 0);
    if (!o.hint.empty()) {
        lv_obj_t* h = Label(p->ov, o.hint, g_fonts.small);
        lv_obj_set_width(h, LV_PCT(100));
        lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
    }
    p->ta = lv_textarea_create(p->ov);
    lv_obj_set_width(p->ta, LV_PCT(100));
    lv_obj_set_flex_grow(p->ta, 1);
    lv_textarea_set_one_line(p->ta, !o.multiline);
    if (!o.multiline) lv_obj_set_flex_grow(p->ta, 0), lv_obj_set_height(p->ta, 84);
    lv_textarea_set_text(p->ta, o.initial.c_str());
    lv_textarea_set_placeholder_text(p->ta, o.placeholder.c_str());
    lv_obj_set_style_text_font(p->ta, g_fonts.body, 0);
    lv_obj_set_style_border_width(p->ta, 2, 0);
    lv_obj_set_style_border_color(p->ta, lv_color_black(), 0);
    lv_obj_set_style_radius(p->ta, 12, 0);
    lv_obj_set_style_pad_hor(p->ta, 16, 0);
    lv_obj_set_style_pad_ver(p->ta, 12, 0);
    p->prev = o.initial;
    lv_obj_add_event_cb(p->ta, [](lv_event_t* e) {
        Prompt* p = g_prompt;
        if (!p || p->busy) return;
        std::string now = lv_textarea_get_text(p->ta);
        const uint32_t cur = lv_textarea_get_cursor_pos(p->ta);
        const size_t cb = ByteOfChar(now, cur);
        if (p->vn && now.size() == p->prev.size() + 1 && cb > 0 && std::isalpha((unsigned char)now[cb - 1])) {
            size_t ws = cb - 1;
            while (ws > 0 && now[ws - 1] != ' ' && now[ws - 1] != '\n' && !std::ispunct((unsigned char)now[ws - 1])) --ws;
            std::string word = now.substr(ws, cb - 1 - ws);
            std::string res = telex::Type(word, now[cb - 1]);
            if (res != word + now[cb - 1]) {
                p->busy = true;
                std::string next = now.substr(0, ws) + res + now.substr(cb);
                lv_textarea_set_text(p->ta, next.c_str());
                lv_textarea_set_cursor_pos(p->ta, (int32_t)(CharsIn(now.substr(0, ws)) + CharsIn(res)));
                p->busy = false;
                now = next;
            }
        }
        p->prev = now;
        (void)e;
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(p->ta, [](lv_event_t*) { ClosePrompt(true); }, LV_EVENT_READY, nullptr);

    p->mode = Button(p->ov, "", [] { if (g_prompt) { g_prompt->vn = !g_prompt->vn; RenderMode(g_prompt); } }, LV_SIZE_CONTENT, 60);
    lv_obj_set_style_pad_hor(p->mode, 18, 0);
    lv_obj_center(Label(p->mode, "", g_fonts.small));
    RenderMode(p);
    lv_obj_t* kb = StyledKeyboard(p->ov, p->ta, 420);
    lv_obj_add_event_cb(kb, [](lv_event_t*) { ClosePrompt(true); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(kb, [](lv_event_t*) { ClosePrompt(false); }, LV_EVENT_CANCEL, nullptr);
    lv_group_focus_obj(p->ta);
    SetBackHandler([] { ClosePrompt(false); });
}

}  // namespace ui

namespace ui {

namespace {

struct BatteryCfg { int h; bool reader; int style; int show; };

const lv_color_t kBatInk = lv_color_hex(0x1a1a18);

lv_obj_t* BatBox(lv_obj_t* parent, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

const lv_font_t* BatFont(int px) {
    const lv_font_t* f = g_fonts.face ? g_fonts.face("sans", px, true) : nullptr;
    return f ? f : g_fonts.small;
}

void BuildBattery(lv_obj_t* box, int pct, bool charging) {
    auto* c = static_cast<BatteryCfg*>(lv_obj_get_user_data(box));
    lv_obj_clean(box);
    const auto& v = settings::Get();
    const int style = c->style >= 0 ? c->style : v.battery_style;
    const bool show = style == 2 || (c->show >= 0 ? c->show != 0 : (c->reader ? v.battery_percent_reader : v.battery_percent));
    pct = std::max(0, std::min(100, pct));
    const int h = c->h;
    std::string num = charging ? std::string(icon::BAT_CHARGE) : std::to_string(pct);
    int x = 0;
    if (style == 0) {   // ring; the number sits inside when shown
        lv_obj_t* arc = lv_arc_create(box);
        lv_obj_set_size(arc, h, h);
        lv_obj_remove_flag(arc, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_group_remove_obj(arc);
        lv_obj_set_style_pad_all(arc, 0, 0);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arc, 0, 0);
        lv_obj_set_style_outline_width(arc, 0, 0);
        lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_outline_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
        lv_arc_set_rotation(arc, 270);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, pct);
        const int w = std::max(2, h / 11);
        lv_obj_set_style_arc_width(arc, w, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0xd2d0c9), LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, w, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, kBatInk, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
        lv_obj_set_pos(arc, 0, 0);
        if (show) {
            lv_obj_t* n = lv_label_create(arc);
            lv_obj_set_style_text_font(n, BatFont(std::max(12, h * (pct == 100 ? 36 : 44) / 100)), 0);
            lv_obj_set_style_text_color(n, kBatInk, 0);
            lv_label_set_text(n, num.c_str());
            lv_obj_center(n);
        }
        x = h;
    } else if (style == 1) {   // classic battery: outline, fill from the left, nub on the right
        const int bh = std::max(10, h * 55 / 100), bw = bh * 2;
        lv_obj_t* body = BatBox(box, bw, bh);
        lv_obj_set_style_border_width(body, std::max(2, bh / 7), 0);
        lv_obj_set_style_border_color(body, kBatInk, 0);
        lv_obj_set_style_radius(body, 3, 0);
        lv_obj_set_pos(body, 0, (h - bh) / 2);
        const int inset = std::max(2, bh / 7) + 2;
        const int fw = (bw - 2 * inset) * pct / 100;
        if (fw > 0) {
            lv_obj_t* fill = BatBox(body, fw, bh - 2 * inset);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(fill, kBatInk, 0);
            lv_obj_set_style_radius(fill, 1, 0);
            lv_obj_set_pos(fill, inset - std::max(2, bh / 7), inset - std::max(2, bh / 7));
        }
        lv_obj_t* nub = BatBox(box, std::max(3, bh / 5), bh / 2);
        lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(nub, kBatInk, 0);
        lv_obj_set_style_radius(nub, 1, 0);
        lv_obj_set_pos(nub, bw + 1, (h - bh / 2) / 2);
        x = bw + std::max(3, bh / 5) + 1;
        if (charging) {
            lv_obj_t* z = lv_label_create(box);
            lv_obj_set_style_text_font(z, IconFont(bh), 0);
            lv_obj_set_style_text_color(z, kBatInk, 0);
            lv_label_set_text(z, icon::BAT_CHARGE);
            lv_obj_update_layout(z);
            lv_obj_set_pos(z, x + 4, (h - lv_obj_get_height(z)) / 2);
            x += 4 + lv_obj_get_width(z);
        }
    }
    if (show && style != 0) {   // number next to the bar, or alone
        lv_obj_t* n = lv_label_create(box);
        lv_obj_set_style_text_font(n, BatFont(std::max(12, h * 62 / 100)), 0);
        lv_obj_set_style_text_color(n, kBatInk, 0);
        lv_label_set_text(n, (std::to_string(pct) + "%").c_str());
        lv_obj_update_layout(n);
        lv_obj_set_pos(n, x ? x + 6 : 0, (h - lv_obj_get_height(n)) / 2);
        x = (x ? x + 6 : 0) + lv_obj_get_width(n);
    }
    lv_obj_set_size(box, std::max(x, 1), h);
}

}  // namespace

lv_obj_t* BatteryIcon(lv_obj_t* parent, int h, bool reader, int style, int show_percent) {
    lv_obj_t* box = BatBox(parent, h, h);
    auto* c = new BatteryCfg{h, reader, style, show_percent};
    lv_obj_set_user_data(box, c);
    lv_obj_add_event_cb(box, [](lv_event_t* e) {
        delete static_cast<BatteryCfg*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
    }, LV_EVENT_DELETE, nullptr);
    BuildBattery(box, platform::BatteryPercent(), platform::Charging());
    return box;
}

void SetBattery(lv_obj_t* box, int pct, bool charging) {
    if (box && lv_obj_get_user_data(box)) BuildBattery(box, pct, charging);
}

}  // namespace ui
