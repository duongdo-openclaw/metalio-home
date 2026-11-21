// "Mục tiêu đọc": the reader sets a daily minutes goal and a yearly books goal; the page shows
// today against the goal, the last 7 days as bars with the goal line, the streak and the year.
#include <algorithm>
#include <cstdio>
#include <ctime>

#include "icons.h"
#include "library.h"
#include "reading_state.h"
#include "screens.h"
#include "settings.h"
#include "ui_common.h"

namespace screens {
namespace {

const lv_color_t kMuted = lv_color_hex(0x5f5d58);
const lv_color_t kHair = lv_color_hex(0xb9b6ae);

lv_obj_t* Rect(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t c) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    return o;
}

lv_obj_t* Panel(lv_obj_t* parent, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, LV_PCT(100), h);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_side(o, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(o, kHair, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

void Chips(lv_obj_t* parent, const std::vector<int>& opts, int cur, const char* unit, std::function<void(int)> pick) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    for (int o : opts) {
        std::string t = o == 0 ? std::string("Tắt") : std::to_string(o) + unit;
        lv_obj_t* b = ui::Button(row, t, [pick, o] { pick(o); }, LV_SIZE_CONTENT, 64);
        lv_obj_set_style_radius(b, 32, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_pad_hor(b, 20, 0);
        lv_obj_set_style_min_width(b, 80, 0);
        ui::Invert(b, o == cur);
    }
}

void Set(std::function<void(settings::Values&)> f) {
    settings::Values v = settings::Get();
    f(v);
    settings::Set(v);
    OpenGoals();
}

}  // namespace

void OpenGoals() {
    ui::Page p = ui::NewPage("MỤC TIÊU ĐỌC", [] { OpenLauncher(); });
    lv_obj_set_style_pad_row(p.body, 12, 0);
    const auto& v = settings::Get();
    reader::Stats st = reader::GetStats();
    std::time_t now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    const int year = tm.tm_year + 1900;
    const int today = st.today_seconds / 60;
    const int goal = v.goal_minutes;
    lv_label_set_text_fmt(p.header_status, "%d ngày đọc liên tục", st.streak_days);

    // Today
    lv_obj_t* t = Panel(p.body, 200);
    lv_obj_t* eb = ui::Label(t, "HÔM NAY", ui::F().small);
    lv_obj_set_style_text_letter_space(eb, 3, 0);
    lv_obj_set_style_text_color(eb, kMuted, 0);
    lv_obj_set_pos(eb, 0, 0);
    char big[32];
    std::snprintf(big, sizeof big, goal ? "%d / %d" : "%d", today, goal);
    lv_obj_t* n = ui::Label(t, big, ui::F().reader(64, false));
    lv_obj_set_pos(n, 0, 28);
    lv_obj_update_layout(n);
    lv_obj_t* unit = ui::Label(t, "phút", ui::F().serif_italic);
    lv_obj_set_pos(unit, lv_obj_get_width(n) + 14, 64);
    std::string msg = !goal ? "Chưa đặt mục tiêu ngày: chọn bên dưới."
                    : today >= goal ? std::string(icon::TROPHY) + "  Đã đạt mục tiêu hôm nay. Tuyệt vời!"
                    : "Còn " + std::to_string(goal - today) + " phút nữa là đạt mục tiêu hôm nay.";
    lv_obj_t* m = ui::Label(t, msg, ui::F().body);
    lv_obj_set_pos(m, 0, 116);
    if (goal) ui::Progress(t, today * 100 / goal, ui::kW - 2 * ui::kPad, 16);
    if (goal) lv_obj_set_pos(lv_obj_get_child(t, -1), 0, 166);

    // Last 7 days
    lv_obj_t* w = Panel(p.body, 250);
    lv_obj_t* wl = ui::Label(w, "7 NGÀY QUA", ui::F().small);
    lv_obj_set_style_text_letter_space(wl, 3, 0);
    lv_obj_set_style_text_color(wl, kMuted, 0);
    int met = 0, maxm = std::max(goal, 1);
    for (int i = 0; i < 7; ++i) { maxm = std::max(maxm, st.week_seconds[i] / 60); if (goal && st.week_seconds[i] / 60 >= goal) ++met; }
    if (goal) {
        lv_obj_t* ml = ui::Label(w, "Đạt " + std::to_string(met) + "/7 ngày", ui::F().small);
        lv_obj_align(ml, LV_ALIGN_TOP_RIGHT, 0, 0);
    }
    const int chart_top = 44, chart_h = 150, colw = (ui::kW - 2 * ui::kPad) / 7;
    static const char* wd[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
    for (int i = 0; i < 7; ++i) {
        int mins = st.week_seconds[i] / 60;
        int h = mins * chart_h / maxm;
        if (mins > 0) h = std::max(h, 4);
        int x = i * colw + colw / 2 - 22;
        Rect(w, x, chart_top + chart_h - h, 44, h, i == 6 ? lv_color_black() : lv_color_hex(0x8a8780));
        lv_obj_t* d = ui::Label(w, wd[st.week_day[i]], i == 6 ? ui::F().body_bold : ui::F().small);
        lv_obj_update_layout(d);
        lv_obj_set_pos(d, i * colw + (colw - lv_obj_get_width(d)) / 2, chart_top + chart_h + 8);
    }
    Rect(w, 0, chart_top + chart_h, ui::kW - 2 * ui::kPad, 2, lv_color_black());
    if (goal) {   // dashed goal line
        int gy = chart_top + chart_h - goal * chart_h / maxm;
        for (int x = 0; x < ui::kW - 2 * ui::kPad; x += 20) Rect(w, x, gy, 10, 2, lv_color_black());
    }

    // Year
    const int done = library::FinishedInYear(year);
    lv_obj_t* y = Panel(p.body, 120);
    lv_obj_t* yl = ui::Label(y, "NĂM " + std::to_string(year), ui::F().small);
    lv_obj_set_style_text_letter_space(yl, 3, 0);
    lv_obj_set_style_text_color(yl, kMuted, 0);
    std::string ytxt = v.goal_books ? std::to_string(done) + " / " + std::to_string(v.goal_books) + " cuốn đã đọc xong"
                                    : std::to_string(done) + " cuốn đã đọc xong";
    lv_obj_set_pos(ui::Label(y, ytxt, ui::F().serif_book), 0, 30);
    if (v.goal_books) {
        int left = std::max(0, v.goal_books - done);
        int months = std::max(1, 12 - tm.tm_mon);
        std::string pace = left == 0 ? "Đã đạt mục tiêu năm!" : "Cần khoảng " + std::to_string((left + months - 1) / months) + " cuốn mỗi tháng";
        lv_obj_t* pl = ui::Label(y, pace, ui::F().small);
        lv_obj_align(pl, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_t* bar = ui::Progress(y, done * 100 / v.goal_books, ui::kW - 2 * ui::kPad, 12);
        lv_obj_set_pos(bar, 0, 86);
    }

    ui::Label(p.body, "Mục tiêu mỗi ngày", ui::F().body_bold);
    Chips(p.body, {0, 10, 15, 20, 30, 45, 60}, goal, "′", [](int o) { Set([o](settings::Values& x) { x.goal_minutes = o; }); });
    ui::Label(p.body, "Số cuốn mỗi năm", ui::F().body_bold);
    Chips(p.body, {0, 6, 12, 24, 36, 52}, v.goal_books, "", [](int o) { Set([o](settings::Values& x) { x.goal_books = o; }); });
    ui::Show(p.screen);
}

}  // namespace screens
