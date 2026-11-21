// "Thông tin chủ máy": name, contact and an "if found" message. Shown on the home screen.
#include <memory>

#include "owner.h"
#include "platform.h"
#include "screens.h"
#include "ui_common.h"

namespace screens {
namespace {

struct State {
    lv_obj_t* name = nullptr;
    lv_obj_t* contact = nullptr;
    lv_obj_t* message = nullptr;
};

lv_obj_t* Field(lv_obj_t* parent, const char* label, const std::string& value, const char* hint, int h) {
    ui::Label(parent, label, ui::F().body_bold);
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, LV_PCT(100), h);
    lv_textarea_set_text(ta, value.c_str());
    lv_textarea_set_placeholder_text(ta, hint);
    lv_textarea_set_one_line(ta, h < 100);
    lv_obj_set_style_text_font(ta, ui::F().body, 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_pad_hor(ta, 16, 0);
    lv_obj_set_style_pad_ver(ta, 12, 0);
    lv_obj_set_style_border_color(ta, lv_color_black(), 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_outline_width(ta, 5, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(ta, lv_color_black(), LV_STATE_FOCUSED);
    return ta;
}

}  // namespace

void OpenOwner() {
    auto* s = new State();
    ui::Page p = ui::NewPage("CHỦ MÁY", [] { OpenApps(); });
    lv_obj_add_event_cb(p.screen, [](lv_event_t* e) { delete static_cast<State*>(lv_event_get_user_data(e)); },
                        LV_EVENT_DELETE, s);
    lv_label_set_text(p.header_status, "Hiện ở trang chủ và khi có người nhặt được máy");
    owner::Info o = owner::Get();
    s->name = Field(p.body, "Tên chủ máy", o.name, "Ví dụ: Nguyễn Văn An", 70);
    s->contact = Field(p.body, "Liên hệ", o.contact, "Số điện thoại hoặc email", 70);
    s->message = Field(p.body, "Lời nhắn khi nhặt được máy", o.message.empty()
                           ? "Nếu bạn nhặt được máy đọc sách này, xin liên hệ số trên. Cảm ơn bạn!" : o.message,
                       "", 150);
    lv_obj_t* note = ui::Label(p.body, "Trên máy thật, gõ bằng bàn phím ảo của firmware. Trong giả lập, gõ bằng bàn phím máy tính.", ui::F().small);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t* save = ui::Button(p.body, "Lưu thông tin", [s] {
        owner::Info i;
        i.name = lv_textarea_get_text(s->name);
        i.contact = lv_textarea_get_text(s->contact);
        i.message = lv_textarea_get_text(s->message);
        if (owner::Save(i)) { ui::Toast("Đã lưu thông tin chủ máy.", 1500); OpenApps(); }
        else ui::Toast("Không lưu được vào thẻ nhớ.");
    }, LV_PCT(100));
    ui::Invert(save, true);
    ui::Show(p.screen);
}

}  // namespace screens
