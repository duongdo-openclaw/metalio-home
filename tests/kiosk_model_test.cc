#include <fstream>
#include <sstream>

#include "check.h"
#include "home_gate.h"
#include "kiosk_model.h"

static std::string Read(const char* name) {
    std::ifstream f(std::string(FIXTURES_DIR) + "/" + name);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    home::Summary s;
    std::string err;
    CHECK(home::ParseSummary(Read("kiosk_summary.json"), s, err));
    CHECK(s.has_env && s.indoor_temp > 26 && s.outdoor_desc == "Nhiều mây");
    CHECK(s.quick.size() == 2);  // card without id dropped
    CHECK(s.quick[0].type == home::DeviceType::Light && s.quick[0].on);
    CHECK(s.quick[1].type == home::DeviceType::Ac && s.quick[1].temp == 29);
    CHECK(s.rooms.size() == 2 && s.rooms[0].title == "Phòng Khách" && s.rooms[0].devices.size() == 2);
    CHECK(s.rooms[0].devices[0].type == home::DeviceType::Curtain);

    // Commands match Kreader's HomeCenterActivity payloads.
    CHECK(home::CommandJson(home::ToggleCommand(s.quick[0])) ==
          R"({"domain":"switch","service":"toggle","service_data":{"entity_id":"switch.den_sofa_switch_3"}})");
    CHECK(home::CommandJson(home::ToggleCommand(s.rooms[0].devices[0])) ==
          R"({"domain":"cover","service":"close_cover","service_data":{"entity_id":"cover.rem_phong_khach_curtain"}})");
    CHECK(home::CommandJson(home::ToggleCommand(s.quick[1])) ==
          R"({"domain":"climate","service":"set_hvac_mode","service_data":{"entity_id":"climate.dieu_hoa_phong_ngu","hvac_mode":"cool"}})");
    CHECK(home::CommandJson(home::AcTempCommand(s.quick[1], 27)) ==
          R"({"domain":"climate","service":"set_temperature","service_data":{"entity_id":"climate.dieu_hoa_phong_ngu","temperature":27}})");

    CHECK(!home::ParseSummary("<html>", s, err));
    CHECK(!home::ParseSummary("{}", s, err));

    // Home gate.
    CHECK(home::UrlHost("http://192.168.1.50:8124/api") == "192.168.1.50");
    CHECK(home::IsPrivateIpv4("192.168.1.50", false));
    CHECK(home::IsPrivateIpv4("10.0.0.5", false));
    CHECK(home::IsPrivateIpv4("172.20.1.1", false));
    CHECK(!home::IsPrivateIpv4("172.32.1.1", false));
    CHECK(!home::IsPrivateIpv4("8.8.8.8", false));
    CHECK(!home::IsPrivateIpv4("zbox.local", false));
    CHECK(!home::IsPrivateIpv4("127.0.0.1", false));
    CHECK(home::IsPrivateIpv4("127.0.0.1", true));
    CHECK(!home::IsPrivateIpv4("192.168.1.1x", false));
    DONE();
}
