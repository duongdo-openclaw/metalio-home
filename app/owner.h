// Device owner details (shown on the home screen and the "if found" card), stored on the SD card.
#pragma once
#include <string>

namespace owner {

struct Info {
    std::string name;      // "Nguyễn Văn A"
    std::string contact;   // phone or e-mail to return the device
    std::string message;   // "Nếu bạn nhặt được máy, xin liên hệ…"
    bool empty() const { return name.empty(); }
};

Info Get();
bool Save(const Info& info);
std::string FirstName(const Info& info);   // Vietnamese given name = last word ("Nguyễn Văn An" -> "An")

}  // namespace owner
