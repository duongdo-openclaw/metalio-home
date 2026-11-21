// "Kiểm tra cảm ứng": tap highlighted targets in sequence, report where taps actually landed.
// Runs in the simulator (with emulated controller profiles) and later on the device itself,
// to tell a mis-mapped panel (taps land elsewhere) from a dead controller (no taps at all).
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "platform.h"
#include "screens.h"
#include "ui_common.h"

namespace screens {
namespace {

constexpr int kCols = 3, kRows = 5;
constexpr int kTimeoutMs = 8000;  // no tap within this time = "missed"

struct Result {
    int target;
    int hit = -1;          // index of the target that received the tap, -1 = background, -2 = timeout
    bool finger_known = false;
    int fx = 0, fy = 0, lx = 0, ly = 0, rx = 0, ry = 0;
};

struct State {
    ui::Page page;
    std::shared_ptr<bool> alive;
    std::vector<lv_obj_t*> targets;
    std::vector<Result> results;
    int current = 0;
    lv_obj_t* grid = nullptr;
    lv_obj_t* info = nullptr;
    lv_timer_t* timeout = nullptr;
    std::vector<lv_obj_t*> dots;
};

void Next(State* s);

lv_area_t TargetArea(State* s, int i) {
    lv_area_t a;
    lv_obj_get_coords(s->targets[i], &a);
    return a;
}

void Dot(State* s, int x, int y, bool hollow) {
    lv_obj_t* d = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(d, 18, 18);
    lv_obj_set_pos(d, x - 9, y - 9);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 3, 0);
    lv_obj_set_style_border_color(d, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(d, hollow ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_black(), 0);
    s->dots.push_back(d);
}

void Record(State* s, int hit) {
    if (s->current >= (int)s->targets.size()) return;
    Result r;
    r.target = s->current;
    r.hit = hit;
    platform::TouchSample t = platform::LastTouch();
    if (hit != -2 && t.valid) {
        r.lx = t.logical_x; r.ly = t.logical_y; r.rx = t.raw_x; r.ry = t.raw_y;
        r.finger_known = t.finger_known; r.fx = t.finger_x; r.fy = t.finger_y;
        Dot(s, r.lx, r.ly, false);                     // filled = where the firmware put the tap
        if (r.finger_known && (r.fx != r.lx || r.fy != r.ly)) Dot(s, r.fx, r.fy, true);  // ring = real finger
    }
    s->results.push_back(r);
    s->current++;
    Next(s);
}

void Summary(State* s) {
    int ok = 0, wrong = 0, missed = 0;
    double err_sum = 0;
    int err_n = 0;
    std::string rows_bad;
    std::vector<int> bad_by_row(kRows, 0);
    for (const auto& r : s->results) {
        if (r.hit == r.target) ok++;
        else if (r.hit == -2) missed++;
        else { wrong++; bad_by_row[r.target / kCols]++; }
        if (r.hit != -2) {
            lv_area_t a = TargetArea(s, r.target);
            double cx = (a.x1 + a.x2) / 2.0, cy = (a.y1 + a.y2) / 2.0;
            err_sum += std::hypot(r.lx - cx, r.ly - cy);
            err_n++;
        }
    }
    for (int i = 0; i < kRows; ++i)
        if (bad_by_row[i]) rows_bad += (rows_bad.empty() ? "" : ", ") + std::to_string(i + 1);

    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "KẾT QUẢ (%s)\nĐúng %d/%d · chạm nhầm ô %d · không nhận %d\nLệch trung bình %.0f px%s%s",
                  platform::TouchDriverInfo(), ok, (int)s->results.size(), wrong, missed,
                  err_n ? err_sum / err_n : 0.0, rows_bad.empty() ? "" : "\nHàng bị lệch: ",
                  rows_bad.c_str());
    std::string verdict;
    if (missed == (int)s->results.size()) verdict = "\n» Không nhận chạm nào: chip cảm ứng không phản hồi (lỗi I2C/reset). Dùng phím cứng.";
    else if (wrong > 0) verdict = "\n» Chạm bị đặt sai chỗ: lỗi ÁNH XẠ toạ độ (xoay/lật), không phải hỏng phần cứng.";
    else if (missed > 0) verdict = "\n» Có lần không nhận: kiểm tra ngắt INT / tốc độ lấy mẫu.";
    else verdict = "\n» Cảm ứng bình thường.";
    lv_obj_set_style_text_font(s->info, ui::F().small, 0);  // result is longer than the prompt
    lv_label_set_text(s->info, (std::string(buf) + verdict).c_str());
    platform::Log("[touch-test] %s%s", buf, verdict.c_str());
    for (const auto& r : s->results) {
        platform::Log("[touch-test] target=%d hit=%d raw=(%d,%d) logical=(%d,%d) finger=%s(%d,%d)", r.target,
                      r.hit, r.rx, r.ry, r.lx, r.ly, r.finger_known ? "" : "?", r.fx, r.fy);
    }
}

void Next(State* s) {
    if (s->timeout) { lv_timer_delete(s->timeout); s->timeout = nullptr; }
    for (int i = 0; i < (int)s->targets.size(); ++i) ui::Invert(s->targets[i], i == s->current);
    if (s->current >= (int)s->targets.size()) {
        Summary(s);
        return;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "Chạm vào ô ĐEN (số %d/%d). Sau 8 giây không nhận sẽ tự bỏ qua.", s->current + 1,
                  (int)s->targets.size());
    lv_label_set_text(s->info, buf);
    s->timeout = lv_timer_create([](lv_timer_t* t) {
        auto* st = static_cast<State*>(lv_timer_get_user_data(t));
        lv_timer_delete(st->timeout);
        st->timeout = nullptr;
        Record(st, -2);
    }, kTimeoutMs, s);
}

void Restart(State* s) {
    lv_obj_set_style_text_font(s->info, ui::F().body, 0);
    for (auto* d : s->dots) lv_obj_delete(d);
    s->dots.clear();
    s->results.clear();
    s->current = 0;
    Next(s);
}

}  // namespace

void OpenTouchTest() {
    auto* s = new State();
    s->page = ui::NewPage("KIỂM TRA CẢM ỨNG", [] { OpenApps(); });
    s->alive = ui::Alive(s->page.screen);
    lv_obj_add_event_cb(s->page.screen, [](lv_event_t* e) {
        auto* st = static_cast<State*>(lv_event_get_user_data(e));
        if (st->timeout) lv_timer_delete(st->timeout);
        for (auto* d : st->dots) lv_obj_delete(d);
        delete st;
    }, LV_EVENT_DELETE, s);
    lv_label_set_text(s->page.header_status, platform::TouchDriverInfo());

    lv_obj_t* body = s->page.body;
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    s->info = ui::Label(body, "", ui::F().body);
    lv_obj_set_width(s->info, LV_PCT(100));
    lv_label_set_long_mode(s->info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(s->info, 236);

    s->grid = lv_obj_create(body);
    lv_obj_set_size(s->grid, LV_PCT(100), 730);
    lv_obj_set_style_pad_all(s->grid, 0, 0);
    lv_obj_set_style_border_width(s->grid, 0, 0);
    lv_obj_remove_flag(s->grid, LV_OBJ_FLAG_SCROLLABLE);
    // A mis-mapped tap can land anywhere: between targets (grid), on the instructions (body) or
    // in the header. Every one of them must count, or the sequence drifts.
    for (lv_obj_t* catcher : {s->grid, body, lv_obj_get_parent(s->page.header_status)}) {
        lv_obj_add_event_cb(catcher, [](lv_event_t* e) {
            if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;  // a child handled it
            Record(static_cast<State*>(lv_event_get_user_data(e)), -1);
        }, LV_EVENT_CLICKED, s);
    }
    const int gw = ui::kW - 2 * ui::kPad, cw = gw / kCols, ch = 730 / kRows;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            int idx = r * kCols + c;
            lv_obj_t* b = ui::Button(s->grid, std::to_string(idx + 1), [s, idx] { Record(s, idx); }, cw - 24, ch - 24);
            lv_obj_set_pos(b, c * cw + 12, r * ch + 12);
            lv_group_remove_obj(b);  // keypad must not "tap" targets for the user
            s->targets.push_back(b);
        }
    }
    ui::Button(body, "Làm lại", [s] { Restart(s); }, LV_PCT(100), 76);
    ui::Show(s->page.screen);
    Next(s);
}

}  // namespace screens
