// Metalio E-Ink4-Plus UI simulator: LVGL 9.5 at the device's logical 684x1216, rendered as
// 16-level grey (e-ink), with emulated FT6336U touch controller profiles.
//
//   ./build/metalio_sim [--server URL] [--token T] [--drive LINK | --drive-file FILE]
//                       [--sd DIR] [--allow-control] [--touch portrait|landscape|dead]
//                       [--scale F] [--script "wait 1500; tap 340 300; shot a.bmp; key esc; quit"]
//                       [--headless]
#include <SDL2/SDL.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <tuple>
#include <sstream>
#include <string>
#include <vector>

#include "../app/screens.h"
#include "../app/settings.h"
#include "../app/ui_common.h"
#include "lvgl.h"
#include "platform_sim.h"
#include "touch_map.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace {

constexpr int W = ui::kW, H = ui::kH;               // LVGL resolution: 680x1212 content area
constexpr int PW = ui::kPanelW, PH = ui::kPanelH;   // panel: 684x1216 (portrait view of 1216x684)
constexpr int IN = ui::kInset;

uint32_t g_lvbuf[W * H];     // LVGL draws here (XRGB8888)
uint32_t g_frame[PW * PH];   // what the e-ink panel shows
bool g_dirty = true;
bool g_gray_mode = false;    // firmware: L8 when tagged grey content is visible, else I1
int g_orient = 0;            // 0 portrait, 1 land-left, 2 land-right, 3 portrait-down (firmware display_orient)
lv_display_t* g_disp = nullptr;
lv_indev_t* g_touch = nullptr;
bool g_window_dirty = false; // window must be resized after an orientation change
int g_vx = -1, g_vy = -1;    // finger in view (window) coordinates
std::vector<uint32_t> g_view;  // the panel as the user sees it (rotated upright)
int LW() { return (g_orient == 1 || g_orient == 2) ? H : W; }   // LVGL width
int LH() { return (g_orient == 1 || g_orient == 2) ? W : H; }
int VW() { return (g_orient == 1 || g_orient == 2) ? PH : PW; } // view (window) size
int VH() { return (g_orient == 1 || g_orient == 2) ? PW : PH; }

// LVGL pixel -> panel content pixel (680x1212) for the current orientation.
void LvToContent(int x, int y, int& cx, int& cy) {
    switch (g_orient) {
    case 1: cx = y; cy = H - 1 - x; break;
    case 2: cx = W - 1 - y; cy = x; break;
    case 3: cx = W - 1 - x; cy = H - 1 - y; break;
    default: cx = x; cy = y;
    }
}
void ContentToLv(int cx, int cy, int& x, int& y) {
    switch (g_orient) {
    case 1: x = H - 1 - cy; y = cx; break;
    case 2: x = cy; y = W - 1 - cx; break;
    case 3: x = W - 1 - cx; y = H - 1 - cy; break;
    default: x = cx; y = cy;
    }
}
// Window/view pixel -> panel pixel (the window shows the device turned the way it is held).
void ViewToPanel(int vx, int vy, int& px, int& py) {
    switch (g_orient) {
    case 1: px = vy; py = PH - 1 - vx; break;
    case 2: px = PW - 1 - vy; py = vx; break;
    case 3: px = PW - 1 - vx; py = PH - 1 - vy; break;
    default: px = vx; py = vy;
    }
}
void BuildView() {
    g_view.resize((size_t)VW() * VH());
    for (int vy = 0; vy < VH(); ++vy)
        for (int vx = 0; vx < VW(); ++vx) {
            int px, py;
            ViewToPanel(vx, vy, px, py);
            g_view[(size_t)vy * VW() + vx] = g_frame[py * PW + px];
        }
}
SDL_Window* g_win = nullptr;
bool g_shot_marker = true;   // draw the finger ring into screenshots

touch_profile_t g_profile = TOUCH_PROFILE_PORTRAIT;
bool g_pressed = false;
int g_lx = 0, g_ly = 0;           // what the firmware reports to LVGL
int g_fx = -1, g_fy = -1;         // where the "finger" really is
std::deque<std::pair<uint32_t, bool>>* g_keys;

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t a = s.find_first_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a);
}

void Flush(lv_display_t* d, const lv_area_t* a, uint8_t*) {
    // Grey mode: 16 levels (epdiy 4 bpp). Binary mode: the firmware renders LV_COLOR_FORMAT_I1,
    // so every pixel - including anti-aliased glyph edges - ends up pure black or white.
    for (int y = a->y1; y <= a->y2; ++y) {
        for (int x = a->x1; x <= a->x2; ++x) {
            uint32_t p = g_lvbuf[y * LW() + x];
            int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            int l = (r * 299 + g * 587 + b * 114) / 1000;
            const int depth = SimDeviceState().black_depth;   // Cài đặt › Màn hình › Độ đậm màu đen
            if (g_gray_mode) {
                l = l - (255 - l) * depth / 6;                     // push greys towards black
                if (l < 0) l = 0;
                l = (l * 15 + 127) / 255 * 17;
            } else {
                l = l >= 128 + depth * 24 ? 255 : 0;              // thicker strokes in 1-bit mode
            }
            int cx, cy;
            LvToContent(x, y, cx, cy);
            g_frame[(cy + IN) * PW + cx + IN] = 0xFF000000u | (l << 16) | (l << 8) | l;
        }
    }
    g_dirty = true;
    lv_display_flush_ready(d);
}

void TouchRead(lv_indev_t*, lv_indev_data_t* data) {
    data->point.x = g_lx;
    data->point.y = g_ly;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void KeyRead(lv_indev_t*, lv_indev_data_t* data) {
    static uint32_t last = 0;
    if (!g_keys->empty()) {
        auto k = g_keys->front();
        g_keys->pop_front();
        last = k.first;
        data->key = k.first;
        data->state = k.second ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = !g_keys->empty();
    } else {
        data->key = last;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void UpdateTouchInfo() {
    sim_set_touch_info(std::string("giả lập: ") + touch_profile_name(g_profile));
}

// Finger at panel (fx, fy) → controller raw → firmware mapping → LVGL (rotated).
void Finger(bool down, int fx, int fy) {
    g_fx = fx;
    g_fy = fy;
    if (!down) {
        g_pressed = false;
        ui::FeedPointer(false, g_lx, g_ly);
        return;
    }
    int rx, ry;
    if (!touch_profile_raw(g_profile, fx, fy, &rx, &ry)) { g_pressed = false; return; }  // dead chip
    int px, py;
    fw_map_to_logical(rx, ry, &px, &py);   // panel logical (684x1216)
    int cx = std::max(0, std::min(W - 1, px - IN));   // firmware then removes the content inset
    int cy = std::max(0, std::min(H - 1, py - IN));
    ContentToLv(cx, cy, g_lx, g_ly);                   // ... and applies the UI orientation
    g_pressed = true;
    static bool owned = false;
    bool was = owned;
    owned = ui::FeedPointer(true, g_lx, g_ly);
    if (owned && !was && g_touch) {   // swipe-down opened quick settings: the press must not click
        lv_indev_reset(g_touch, nullptr);
        lv_indev_wait_release(g_touch);
    }
    platform::TouchSample s;
    s.valid = true;
    s.finger_known = true;
    s.finger_x = fx; s.finger_y = fy;
    s.raw_x = rx; s.raw_y = ry;
    s.logical_x = g_lx; s.logical_y = g_ly;
    s.finger_x = fx - IN; s.finger_y = fy - IN;   // report in UI coordinates like logical_*
    sim_set_touch(s);
}

void QueueKey(uint32_t k) {
    g_keys->push_back({k, true});
    g_keys->push_back({k, false});
}

bool HandleKeyName(const std::string& name) {
    ui::NoteActivity();
    // Device keys by name: ai / aidouble / aihold, home / homehold, vkback / vkbackhold,
    // vknext / vknexthold, volup / voluphold, voldown / voldownhold, power / powerhold.
    struct K { const char* n; screens::HwKey k; screens::Press p; };
    static const K hw[] = {
        {"power", screens::HwKey::Power, screens::Press::Click}, {"powerhold", screens::HwKey::Power, screens::Press::Long},
        {"ai", screens::HwKey::Ai, screens::Press::Click}, {"aidouble", screens::HwKey::Ai, screens::Press::Double},
        {"aihold", screens::HwKey::Ai, screens::Press::Long}, {"aiup", screens::HwKey::Ai, screens::Press::Up}, {"home", screens::HwKey::Home, screens::Press::Click},
        {"homehold", screens::HwKey::Home, screens::Press::Long}, {"vkback", screens::HwKey::Back, screens::Press::Click},
        {"vkbackhold", screens::HwKey::Back, screens::Press::Long}, {"vknext", screens::HwKey::Next, screens::Press::Click},
        {"vknexthold", screens::HwKey::Next, screens::Press::Long}, {"volup", screens::HwKey::VolUp, screens::Press::Click},
        {"voluphold", screens::HwKey::VolUp, screens::Press::Long}, {"voldown", screens::HwKey::VolDown, screens::Press::Click},
        {"voldownhold", screens::HwKey::VolDown, screens::Press::Long}};
    for (const auto& h : hw) if (name == h.n) { screens::OnKey(h.k, h.p); return true; }
    if (screens::WakeIfStandby()) return true;
    if (name == "quick") { screens::OpenQuickSettings(); return true; }
    if (name == "pgdn") { if (!ui::InvokePageKey(+1)) QueueKey(LV_KEY_NEXT); return true; }
    if (name == "pgup") { if (!ui::InvokePageKey(-1)) QueueKey(LV_KEY_PREV); return true; }
    if (name == "esc" || name == "back") { ui::InvokeBack(); return true; }
    if (name == "next" || name == "down" || name == "tab") { QueueKey(LV_KEY_NEXT); return true; }
    if (name == "prev" || name == "up") { QueueKey(LV_KEY_PREV); return true; }
    if (name == "enter" || name == "ok") { QueueKey(LV_KEY_ENTER); return true; }
    return false;
}

bool SaveShot(const std::string& path) {
    BuildView();   // screenshots show the device the right way up
    std::vector<uint32_t> img = g_view;
    if (g_vx >= 0 && g_shot_marker) {  // simulator chrome: red ring = finger
        for (int a = 0; a < 360; a += 2) {
            for (int rr = 14; rr <= 17; ++rr) {
                int x = g_vx + (int)(rr * SDL_cos(a * M_PI / 180)), y = g_vy + (int)(rr * SDL_sin(a * M_PI / 180));
                if (x >= 0 && x < VW() && y >= 0 && y < VH()) img[y * VW() + x] = 0xFFFF0000u;
            }
        }
    }
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(img.data(), VW(), VH(), 32, VW() * 4, SDL_PIXELFORMAT_ARGB8888);
    int rc = SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
    std::fprintf(stderr, "[sim] screenshot %s %s\n", path.c_str(), rc == 0 ? "ok" : SDL_GetError());
    return rc == 0;
}

struct Step { std::string cmd; std::vector<std::string> args; };

std::vector<Step> ParseScript(const std::string& s) {
    std::vector<Step> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ';')) {
        std::stringstream ps(part);
        Step st;
        ps >> st.cmd;
        std::string a;
        while (ps >> a) st.args.push_back(a);
        if (!st.cmd.empty()) out.push_back(st);
    }
    return out;
}

// Monitor geometry from xrandr: X11 here runs a fractionally scaled desktop (e.g. 5112x2130 drawn
// onto a 3840x1600 panel), so "1 window pixel" is not "1 monitor pixel". SDL's DPI is unreliable.
struct Monitor { double x_per_phys = 1.0; double phys_ppi = 0; int desktop_h = 0; bool ok = false; std::string desc; };

Monitor DetectMonitor() {
    Monitor m;
    FILE* p = popen("xrandr --current 2>/dev/null", "r");
    if (!p) return m;
    char line[512];
    int sw = 0, sh = 0, mw = 0, mh = 0, mm_w = 0, mm_h = 0;
    bool in_primary = false;
    while (std::fgets(line, sizeof line, p)) {
        std::string l = line;
        if (l.find(" connected") != std::string::npos) {
            in_primary = l.find(" primary ") != std::string::npos || sw == 0;
            if (in_primary) {
                size_t at = l.find(" primary ") != std::string::npos ? l.find(" primary ") + 9 : l.find(" connected ") + 11;
                std::sscanf(l.c_str() + at, "%dx%d", &sw, &sh);
                size_t mmp = l.find("mm x ");
                if (mmp != std::string::npos) {
                    size_t b = l.rfind(' ', mmp - 1);
                    mm_w = std::atoi(l.c_str() + b + 1);
                    mm_h = std::atoi(l.c_str() + mmp + 5);
                }
            }
        } else if (in_primary && mw == 0 && l.find('*') != std::string::npos) {
            std::sscanf(l.c_str(), " %dx%d", &mw, &mh);
        }
    }
    pclose(p);
    if (sw > 0 && mw > 0) {
        m.ok = true;
        m.x_per_phys = (double)sw / mw;
        m.desktop_h = sh;
        if (mm_w > 0) m.phys_ppi = mw / (mm_w / 25.4);
        char d[160];
        std::snprintf(d, sizeof d, "màn %dx%d thật, desktop X %dx%d (x%.3f), %.0f ppi", mw, mh, sw, sh, m.x_per_phys, m.phys_ppi);
        m.desc = d;
    }
    return m;
}

std::string g_icon_ttf;   // Lucide line icons (assets/Lucide-icons.ttf), fallback of every text font

lv_font_t* IconFont(int px) {
    static std::map<int, lv_font_t*> cache;
    auto it = cache.find(px);
    if (it != cache.end()) return it->second;
    lv_font_t* f = g_icon_ttf.empty() ? nullptr : lv_tiny_ttf_create_data_ex(g_icon_ttf.data(), g_icon_ttf.size(), px, LV_FONT_KERNING_NONE, 128);
    if (f) f->fallback = &lv_font_montserrat_28;   // keyboard / LV_SYMBOL glyphs still resolve
    cache[px] = f;
    return f;
}

// Private-use code points (Lucide icons, LV_SYMBOL_*) skip the text face and go straight to its
// fallback: asking tiny_ttf for a glyph it lacks logs an error on every draw.
bool RouteGlyph(const lv_font_t* f, lv_font_glyph_dsc_t* d, uint32_t letter, uint32_t next) {
    if (letter >= 0xE000 && letter <= 0xF8FF) return false;
    const lv_font_t* base = static_cast<const lv_font_t*>(f->user_data);
    return base->get_glyph_dsc(f, d, letter, next);   // same dsc: the copy shares the TTF state
}

lv_font_t* LoadFont(const std::string& data, int px) {
    // Vietnamese text uses many distinct glyphs per screen; the default 128-entry cache runs out.
    lv_font_t* f = lv_tiny_ttf_create_data_ex(data.data(), data.size(), px, LV_FONT_KERNING_NORMAL, 1024);
    if (!f) return nullptr;
    // Icons inside text ("🔋 80%", "Thư viện ›"): glyphs missing from the face come from Lucide,
    // drawn a little larger than the text so the 2 px strokes stay crisp.
    auto* r = new lv_font_t(*f);
    r->user_data = f;
    r->get_glyph_dsc = RouteGlyph;
    r->fallback = IconFont(px * 115 / 100);
    return r;
}

}  // namespace

namespace platform {
// Simulator: rotate the LVGL display (firmware: DisplayUiSetOrient changes the resolution too).
void SetDisplayOrientation(int orient) {
    orient = std::max(0, std::min(3, orient));
    if (orient == g_orient || !g_disp) return;
    g_orient = orient;
    lv_display_set_resolution(g_disp, LW(), LH());
    lv_display_set_buffers(g_disp, g_lvbuf, nullptr, sizeof g_lvbuf, LV_DISPLAY_RENDER_MODE_DIRECT);
    for (auto& px : g_frame) px = 0xFFFFFFFFu;
    g_window_dirty = true;
    g_dirty = true;
    static const char* names[] = {"dọc", "ngang trái", "ngang phải", "dọc ngược"};
    std::fprintf(stderr, "[sim] hướng màn hình: %s (LVGL %dx%d)\n", names[orient], LW(), LH());
}
int DisplayOrientation() { return g_orient; }
}  // namespace platform

int main(int argc, char** argv) {
    platform::Config cfg;
    cfg.kiosk_base_url = "http://127.0.0.1:8124";
    std::string drive_file = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config/kreader/drive_folder";
    std::string sd = "sdcard", script;
    float scale = -2.0f;   // default: 1 device pixel = 1 physical monitor pixel
    bool headless = false, fake_mic = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--server") cfg.kiosk_base_url = next();
        else if (a == "--token") cfg.kiosk_token = next();
        else if (a == "--drive") cfg.drive_folder = next();
        else if (a == "--drive-file") drive_file = next();
        else if (a == "--sd") sd = next();
        else if (a == "--allow-control") cfg.allow_control = true;
        else if (a == "--scale") { std::string v = next(); scale = v == "real" ? -1.0f : v == "pixel" ? -2.0f : std::stof(v); }
        else if (a == "--script") script = next();
        else if (a == "--headless") headless = true;
        else if (a == "--fake-mic") fake_mic = true;
        else if (a == "--clean-shots") g_shot_marker = false;
        else if (a == "--touch") {
            std::string p = next();
            g_profile = p == "landscape" ? TOUCH_PROFILE_LANDSCAPE : p == "dead" ? TOUCH_PROFILE_DEAD : TOUCH_PROFILE_PORTRAIT;
        } else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (cfg.drive_folder.empty()) cfg.drive_folder = Trim(ReadFile(drive_file));
    cfg.books_dir = sd + "/books";
    ::mkdir(sd.c_str(), 0755);
    ::mkdir(cfg.books_dir.c_str(), 0755);

    if (headless) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    if (scale < 0 && !headless) {
        Monitor mon = DetectMonitor();
        if (mon.ok) std::fprintf(stderr, "[sim] %s\n", mon.desc.c_str());
        if (scale == -2.0f) {  // pixel: each panel pixel on exactly one monitor pixel
            scale = (float)mon.x_per_phys;
            std::fprintf(stderr, "[sim] 1:1 điểm ảnh: cửa sổ %dx%d px desktop = %dx%d px thật trên màn\n",
                         (int)(PW * scale + 0.5f), (int)(PH * scale + 0.5f), PW, PH);
        } else {               // real: same physical size as the 4.7" panel (297 ppi)
            double ppi = mon.phys_ppi > 0 ? mon.phys_ppi : 96.0;
            scale = (float)(mon.x_per_phys * ppi / ui::kPpi);
            std::fprintf(stderr, "[sim] kích thước thật: %.1f x %.1f cm (màn %.0f ppi, máy đọc sách %.0f ppi)\n",
                         PW / ui::kPpi * 2.54, PH / ui::kPpi * 2.54, ppi, ui::kPpi);
        }
        // SDL's usable bounds are in panel pixels under fractional X scaling; use the X desktop height.
        SDL_Rect usable{0, 0, 0, 0};
        int avail = mon.ok ? mon.desktop_h - 140 : (SDL_GetDisplayUsableBounds(0, &usable) == 0 ? usable.h - 40 : 0);
        if (avail > 0 && PH * scale > avail) {
            float fit = (float)avail / PH;
            std::fprintf(stderr, "[sim] cửa sổ cao %d px không vừa màn hình (%d px) -> thu nhỏ x%.2f\n", (int)(PH * scale), avail, fit);
            scale = fit;
        }
    }
    if (scale <= 0) scale = 1.0f;
    // Nearest-neighbour: keep e-ink pixels hard-edged instead of smoothing them.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* win = SDL_CreateWindow("Metalio E-Ink4-Plus · 684x1216 · giả lập", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       (int)(PW * scale + 0.5f), (int)(PH * scale + 0.5f), 0);
    g_win = win;
    for (auto& px : g_frame) px = 0xFFFFFFFFu;  // the 2 px inset stays white, as on the device
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, headless ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PW, PH);

    sim_platform_init(cfg, sd);
    sim_audio_set_fake_mic(fake_mic);
    sim_device_set_sd(sd);
    sim_transfer_set_sd(sd);
    UpdateTouchInfo();

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return SDL_GetTicks(); });
    lv_display_t* disp = lv_display_create(W, H);
    g_disp = disp;
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(disp, g_lvbuf, nullptr, sizeof g_lvbuf, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, Flush);
    // Same decision point as the firmware (lv_adapter_epdiy.cc, LV_EVENT_REFR_START).
    lv_display_add_event_cb(disp, [](lv_event_t* e) {
        auto* d = static_cast<lv_display_t*>(lv_event_get_target(e));
        bool gray = ui::GrayContentVisible();
        if (gray == g_gray_mode) return;
        g_gray_mode = gray;
        lv_obj_invalidate(lv_display_get_screen_active(d));
        lv_obj_invalidate(lv_display_get_layer_top(d));
        std::fprintf(stderr, "[sim] content render format=%s\n", gray ? "L8 (16 mức xám)" : "I1 (đen trắng)");
        if (g_win) SDL_SetWindowTitle(g_win, gray ? "Metalio · giả lập · L8 xám" : "Metalio · giả lập · I1 đen trắng");
    }, LV_EVENT_REFR_START, nullptr);

    g_icon_ttf = ReadFile(std::string(ASSETS_DIR) + "/Lucide-icons.ttf");
    std::string reg = ReadFile(std::string(ASSETS_DIR) + "/NotoSans-Regular.ttf");
    std::string bold = ReadFile(std::string(ASSETS_DIR) + "/NotoSans-Bold.ttf");
    std::string serif = ReadFile(std::string(ASSETS_DIR) + "/NotoSerif-Regular.ttf");
    std::string serif_b = ReadFile(std::string(ASSETS_DIR) + "/NotoSerif-Bold.ttf");
    std::string serif_i = ReadFile(std::string(ASSETS_DIR) + "/NotoSerif-Italic.ttf");
    if (reg.empty() || bold.empty() || serif.empty() || serif_b.empty() || serif_i.empty() || g_icon_ttf.empty()) { std::fprintf(stderr, "fonts missing in %s\n", ASSETS_DIR); return 1; }
    ui::Fonts fonts;
    fonts.small = LoadFont(reg, 25);   // stock UI: MiSans 25; <24 px loses Vietnamese marks in 1-bit
    fonts.body = LoadFont(reg, 28);
    fonts.body_bold = LoadFont(bold, 28);
    fonts.title = LoadFont(bold, 36);
    fonts.big = LoadFont(bold, 52);
    fonts.icon = IconFont(36);
    fonts.icon_big = IconFont(56);
    fonts.icon_px = [](int px) -> const lv_font_t* { return IconFont(px); };
    // Reader text at any size; created once per (size, weight) and kept for the session.
    fonts.serif_title = LoadFont(serif_b, 40);
    fonts.serif_book = LoadFont(serif_b, 30);
    fonts.serif_italic = LoadFont(serif_i, 24);
    // Reading faces: built-in serif/sans, plus TTF/OTF files the user copies to <sd>/fonts.
    static std::map<std::string, std::string> face_data;   // keeps font bytes alive for tiny_ttf
    fonts.face = [&serif, &serif_b, &reg, &bold](const std::string& family, int px, bool b) -> const lv_font_t* {
        static std::map<std::tuple<std::string, int, bool>, lv_font_t*> cache;
        auto key = std::make_tuple(family, px, b);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        const std::string* data = nullptr;
        if (family == "sans") data = b ? &bold : &reg;
        else if (family.rfind("file:", 0) == 0) {
            std::string& d = face_data[family];
            if (d.empty()) d = ReadFile(platform::FontsDir() + "/" + family.substr(5));
            data = d.empty() ? nullptr : &d;   // one weight per file: "bold" uses the same file
        } else data = b ? &serif_b : &serif;
        lv_font_t* f = data ? LoadFont(*data, px) : nullptr;
        cache[key] = f;
        return f;
    };
    fonts.reader = [&serif, &serif_b](int px, bool b) -> const lv_font_t* {
        static std::map<std::pair<int, bool>, lv_font_t*> cache;
        auto key = std::make_pair(px, b);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        lv_font_t* f = LoadFont(b ? serif_b : serif, px);
        cache[key] = f;
        return f;
    };
    ui::SetFonts(fonts);
    lv_display_set_theme(disp, lv_theme_mono_init(disp, false, fonts.body));

    std::deque<std::pair<uint32_t, bool>> keys;
    g_keys = &keys;
    lv_indev_t* touch = lv_indev_create();
    g_touch = touch;
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, TouchRead);
    lv_indev_t* kp = lv_indev_create();
    lv_indev_set_type(kp, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kp, KeyRead);
    lv_group_t* group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(kp, group);

    ui::SetNavHandler([](ui::Tab t) {
        if (t == ui::Tab::Home) { if (screens::HomeActive()) screens::OpenLibrary(); else screens::OpenLauncher(); }
        else if (t == ui::Tab::Discover) screens::OpenDriveLibrary();
        else if (t == ui::Tab::Assistant) screens::OpenAssistant();
        else screens::OpenApps();
    });
    SDL_StartTextInput();
    settings::Get();          // load + apply frontlight, black depth…
    ui::ApplyUiFont(settings::Get().ui_font);
    screens::OpenLauncher();
    screens::InstallSystem();

    std::vector<Step> steps = ParseScript(script);
    size_t step = 0;
    uint32_t wait_until = 0;
    bool quit = false;
    int tap_phase = 0;  // scripted tap: 1 = pressed, release next tick
    uint32_t tap_release_at = 0;

    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: quit = true; break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONUP: {
                int mx = ev.type == SDL_MOUSEMOTION ? ev.motion.x : ev.button.x;
                int my = ev.type == SDL_MOUSEMOTION ? ev.motion.y : ev.button.y;
                bool down = ev.type == SDL_MOUSEBUTTONDOWN || (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_LMASK));
                if (ev.type == SDL_MOUSEMOTION && !down && !g_pressed) break;
                ui::NoteActivity();
                g_vx = (int)(mx / scale);
                g_vy = (int)(my / scale);
                int px, py;
                ViewToPanel(std::max(0, std::min(VW() - 1, g_vx)), std::max(0, std::min(VH() - 1, g_vy)), px, py);
                Finger(down, px, py);
                g_dirty = true;
                break;
            }
            case SDL_TEXTINPUT: {  // typing (incl. Vietnamese IME) into the focused text field
                lv_obj_t* f = lv_group_get_focused(group);
                if (f && lv_obj_check_type(f, &lv_textarea_class)) lv_textarea_add_text(f, ev.text.text);
                break;
            }
            case SDL_KEYUP:
                // AI key push-to-talk: F3 (or Shift+F2) held = talking, release = send.
                if (ev.key.keysym.sym == SDLK_F3 || (ev.key.keysym.sym == SDLK_F2 && (ev.key.keysym.mod & KMOD_SHIFT)))
                    screens::OnKey(screens::HwKey::Ai, screens::Press::Up);
                break;
            case SDL_KEYDOWN: {
                SDL_Keycode k = ev.key.keysym.sym;
                ui::NoteActivity();
                // Device keys (Shift = hold): F4 power, F2 AI (F3 hold, Ctrl+F2 double), F6 Home,
                // F7 cover Back/prev, F8 cover Next, F9 / F10 volume + / −.
                {
                    const bool hold = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
                    const screens::Press pr = hold ? screens::Press::Long : screens::Press::Click;
                    if (k == SDLK_F4) { screens::OnKey(screens::HwKey::Power, pr); break; }
                    if (k == SDLK_F2) { if (!ev.key.repeat) screens::OnKey(screens::HwKey::Ai, (ev.key.keysym.mod & KMOD_CTRL) ? screens::Press::Double : pr); break; }
                    if (k == SDLK_F3) { if (!ev.key.repeat) screens::OnKey(screens::HwKey::Ai, screens::Press::Long); break; }
                    if (k == SDLK_F6) { screens::OnKey(screens::HwKey::Home, pr); break; }
                    if (k == SDLK_F7) { screens::OnKey(screens::HwKey::Back, pr); break; }
                    if (k == SDLK_F8) { screens::OnKey(screens::HwKey::Next, pr); break; }
                    if (k == SDLK_F9) { screens::OnKey(screens::HwKey::VolUp, pr); break; }
                    if (k == SDLK_F10) { screens::OnKey(screens::HwKey::VolDown, pr); break; }
                }
                if (screens::WakeIfStandby()) break;                              // any key wakes the device
                lv_obj_t* focused = lv_group_get_focused(group);
                bool typing = focused && lv_obj_check_type(focused, &lv_textarea_class);
                if (typing && k == SDLK_BACKSPACE) { lv_textarea_delete_char(focused); break; }
                if (typing && (k == SDLK_LEFT || k == SDLK_RIGHT)) {
                    if (k == SDLK_LEFT) lv_textarea_cursor_left(focused); else lv_textarea_cursor_right(focused);
                    break;
                }
                if (typing && k == SDLK_RETURN) { QueueKey(LV_KEY_NEXT); break; }
                if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) ui::InvokeBack();
                else if ((k == SDLK_PAGEDOWN || k == SDLK_RIGHT || k == SDLK_PAGEUP || k == SDLK_LEFT) &&
                         ev.key.repeat && !settings::Get().hold_to_flip) { /* held key: one page only */ }
                else if (k == SDLK_PAGEDOWN || k == SDLK_RIGHT) { if (!ui::InvokePageKey(+1)) QueueKey(LV_KEY_NEXT); }
                else if (k == SDLK_PAGEUP || k == SDLK_LEFT) { if (!ui::InvokePageKey(-1)) QueueKey(LV_KEY_PREV); }
                else if (k == SDLK_DOWN || (k == SDLK_TAB && !(ev.key.keysym.mod & KMOD_SHIFT))) QueueKey(LV_KEY_NEXT);
                else if (k == SDLK_UP || k == SDLK_TAB) QueueKey(LV_KEY_PREV);
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) QueueKey(LV_KEY_ENTER);
                else if (k == SDLK_F1) {
                    g_profile = (touch_profile_t)((g_profile + 1) % TOUCH_PROFILE_COUNT);
                    UpdateTouchInfo();
                    std::fprintf(stderr, "[sim] touch profile: %s\n", touch_profile_name(g_profile));
                } else if (k == SDLK_F12) SaveShot("shot.bmp");
                else if (k == SDLK_F5) screens::OpenQuickSettings();   // = swipe down from the top edge
                break;
            }
            }
        }

        // Scripted steps (for automated checks).
        uint32_t now = SDL_GetTicks();
        if (tap_phase == 1 && now >= tap_release_at) { Finger(false, g_fx, g_fy); tap_phase = 0; }
        while (step < steps.size() && now >= wait_until && tap_phase == 0) {
            const Step& st = steps[step++];
            if (st.cmd == "wait") wait_until = now + (st.args.empty() ? 500 : std::stoul(st.args[0]));
            else if (st.cmd == "idle") {  // wait until all HTTP requests are done (max N ms)
                uint32_t limit = now + (st.args.empty() ? 15000 : std::stoul(st.args[0]));
                if (sim_platform_busy() && now < limit) { --step; wait_until = now + 50; steps[step].args = {std::to_string(limit - now)}; }
            } else if (st.cmd == "tap" && st.args.size() == 2) {
                g_vx = std::stoi(st.args[0]);   // script taps are in view (window) coordinates
                g_vy = std::stoi(st.args[1]);
                int px, py;
                ViewToPanel(std::max(0, std::min(VW() - 1, g_vx)), std::max(0, std::min(VH() - 1, g_vy)), px, py);
                Finger(true, px, py);
                tap_phase = 1;
                tap_release_at = now + 80;
            } else if (st.cmd == "hold" && st.args.size() >= 2) {   // hold X Y [ms]: long press
                g_vx = std::stoi(st.args[0]);
                g_vy = std::stoi(st.args[1]);
                int px, py;
                ViewToPanel(std::max(0, std::min(VW() - 1, g_vx)), std::max(0, std::min(VH() - 1, g_vy)), px, py);
                Finger(true, px, py);
                tap_phase = 1;
                tap_release_at = now + (st.args.size() > 2 ? std::stoul(st.args[2]) : 900);
            } else if (st.cmd == "swipe" && st.args.size() == 4) {   // swipe X1 Y1 X2 Y2 (view coords)
                int x1 = std::stoi(st.args[0]), y1 = std::stoi(st.args[1]), x2 = std::stoi(st.args[2]), y2 = std::stoi(st.args[3]);
                for (int k = 0; k <= 8; ++k) {
                    g_vx = x1 + (x2 - x1) * k / 8;
                    g_vy = y1 + (y2 - y1) * k / 8;
                    int px, py;
                    ViewToPanel(std::max(0, std::min(VW() - 1, g_vx)), std::max(0, std::min(VH() - 1, g_vy)), px, py);
                    Finger(true, px, py);
                    lv_timer_handler();
                }
                tap_phase = 1;
                tap_release_at = now + 80;
            } else if (st.cmd == "key" && !st.args.empty()) HandleKeyName(st.args[0]);
            else if (st.cmd == "shot" && !st.args.empty()) { lv_refr_now(disp); SaveShot(st.args[0]); }
            else if (st.cmd == "profile" && !st.args.empty()) {
                g_profile = st.args[0] == "landscape" ? TOUCH_PROFILE_LANDSCAPE : st.args[0] == "dead" ? TOUCH_PROFILE_DEAD : TOUCH_PROFILE_PORTRAIT;
                UpdateTouchInfo();
            } else if (st.cmd == "type") {
                lv_obj_t* f = lv_group_get_focused(group);
                std::string text;
                for (size_t ai = 0; ai < st.args.size(); ++ai) text += (ai ? " " : "") + st.args[ai];
                // One character at a time, like key presses (the Telex input method reacts per key).
                if (f && lv_obj_check_type(f, &lv_textarea_class))
                    for (size_t ci = 0; ci < text.size();) {
                        unsigned char c = (unsigned char)text[ci];
                        size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                        lv_textarea_add_text(f, text.substr(ci, n).c_str());
                        ci += n;
                    }
            } else if (st.cmd == "open" && !st.args.empty()) {   // open SCREEN[:arg] directly (UI review)
                const std::string n = st.args[0];
                const std::string arg = n.find(':') == std::string::npos ? "" : n.substr(n.find(':') + 1);
                const std::string key = n.substr(0, n.find(':'));
                const std::string book = "sdcard/English/Two Short Fables.txt";
                if (key == "home") screens::OpenLauncher();
                else if (key == "library") screens::OpenLibrary();
                else if (key == "apps") screens::OpenApps();
                else if (key == "settings") { if (arg.empty()) screens::OpenSettings(); else screens::OpenSettingsSection(std::stoi(arg)); }
                else if (key == "key") screens::OpenKeySettings(std::stoi(arg));
                else if (key == "reader") screens::OpenReader(arg.empty() ? book : arg);
                else if (key == "assistant") screens::OpenAssistant(arg);
                else if (key == "dict") screens::OpenDictionary(arg);
                else if (key == "vocab") screens::OpenVocab();
                else if (key == "notes") screens::OpenNotes();
                else if (key == "goals") screens::OpenGoals();
                else if (key == "music") screens::OpenMusic();
                else if (key == "recorder") screens::OpenRecorder();
                else if (key == "smarthome") screens::OpenSmartHome();
                else if (key == "touch") screens::OpenTouchTest();
                else if (key == "owner") screens::OpenOwner();
                else if (key == "transfer") screens::OpenTransfer();
                else if (key == "online") screens::OpenOnlineLibraries();
                else if (key == "share") screens::OpenShareBooks("Two Short Fables", {book});
                else if (key == "sharelink") screens::OpenShareLink("Sách văn học", "https://drive.google.com/drive/folders/1DemoFolderAAAAAAAAAAAAAAAAAAAA");
                else if (key == "receive") screens::OpenReceive();
                else if (key == "quick") screens::OpenQuickSettings();
                else if (key == "standby") screens::ShowStandby();
                else std::fprintf(stderr, "[sim] open: unknown screen %s\n", key.c_str());
            } else if (st.cmd == "quit") quit = true;
            else std::fprintf(stderr, "[sim] unknown script step %s\n", st.cmd.c_str());
        }
        if (!steps.empty() && step >= steps.size() && headless && now >= wait_until && tap_phase == 0) quit = true;

        sim_platform_poll();
        sim_transfer_poll();
        sim_device_poll();
        uint32_t wait = lv_timer_handler();
        SimDevice& dev = SimDeviceState();
        if (dev.flash_frames > 0) {  // full refresh: the panel flashes black, then shows the page again
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
            SDL_RenderClear(ren);
            SDL_RenderPresent(ren);
            SDL_Delay(110);
            dev.flash_frames = 0;
            g_dirty = true;
        }
        if (dev.dirty) { g_dirty = true; dev.dirty = false; }
        if (g_window_dirty) {   // orientation changed: the window turns with the device
            SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, VW(), VH());
            SDL_SetWindowSize(win, (int)(VW() * scale + 0.5f), (int)(VH() * scale + 0.5f));
            static const char* t[] = {"dọc", "ngang trái", "ngang phải", "dọc ngược"};
            SDL_SetWindowTitle(win, (std::string("Metalio · giả lập · ") + t[g_orient]).c_str());
            g_window_dirty = false;
        }
        if (g_dirty) {
            BuildView();
            SDL_UpdateTexture(tex, nullptr, g_view.data(), VW() * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            {   // frontlight: e-ink paper is greyish without light; warm/cool LEDs tint and brighten it
                struct C { int r, g, b; };
                const C off{208, 205, 196}, warm{255, 232, 196}, cool{232, 242, 255}, both{255, 246, 232};
                C target = dev.light_mode == 1 ? warm : dev.light_mode == 2 ? cool : dev.light_mode == 3 ? both : off;
                int k = dev.light_mode == 0 ? 0 : dev.brightness;
                C c{off.r + (target.r - off.r) * k / 100, off.g + (target.g - off.g) * k / 100, off.b + (target.b - off.b) * k / 100};
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_MOD);
                SDL_SetRenderDrawColor(ren, (Uint8)c.r, (Uint8)c.g, (Uint8)c.b, 255);
                SDL_RenderFillRect(ren, nullptr);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            }
            if (g_fx >= 0 && g_pressed) {  // overlay: finger (red) vs registered point (blue)
                SDL_SetRenderDrawColor(ren, 220, 0, 0, 255);
                SDL_Rect f{(int)(g_vx * scale) - 8, (int)(g_vy * scale) - 8, 16, 16};
                SDL_RenderDrawRect(ren, &f);
                SDL_SetRenderDrawColor(ren, 0, 80, 255, 255);
                int x = (int)((g_lx + IN) * scale), y = (int)((g_ly + IN) * scale);
                SDL_RenderDrawLine(ren, x - 10, y, x + 10, y);
                SDL_RenderDrawLine(ren, x, y - 10, x, y + 10);
            }
            SDL_RenderPresent(ren);
            g_dirty = false;
        }
        SDL_Delay(wait > 10 ? 10 : (wait ? wait : 1));
    }

    platform::TransferStop();   // join the server thread before globals are destroyed
    while (platform::StorageFormatting()) SDL_Delay(20);   // never quit half-way through a format
    sim_device_poll();
    sim_audio_shutdown();
    sim_platform_shutdown();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
