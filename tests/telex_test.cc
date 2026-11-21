#include <string>

#include "check.h"
#include "telex.h"

int main() {
    struct { const char* in; const char* out; } cases[] = {
        {"Vieejt Nam", "Việt Nam"}, {"tieengs", "tiếng"}, {"dduowngf", "đường"}, {"hoaf", "hòa"},
        {"quas", "quá"}, {"gif", "gì"}, {"giaf", "già"}, {"nguyeenx", "nguyễn"}, {"khoer", "khỏe"},
        {"muaf", "mùa"}, {"cuar", "của"}, {"tooi", "tôi"}, {"ddocj sachs", "đọc sách"}, {"ass", "as"},
        {"Tw", "Tư"}, {"luwu", "lưu"}, {"chuwong", "chương"}, {"nguwowif", "người"}, {"duowng", "dương"}, {"hoangf", "hoàng"},
        {"Ddaatj", "Đật"}, {"mootj", "một"}, {"tuwj", "tự"}, {"nghieeng", "nghiêng"}, {"bawts", "bắt"},
        {"hoc tieengs Anh", "hoc tiếng Anh"},
    };
    for (auto& c : cases) {
        std::string got = telex::Convert(c.in);
        if (got != c.out) std::printf("  %s -> %s (want %s)\n", c.in, got.c_str(), c.out);
        CHECK(got == c.out);
    }
    DONE();
}
