// Data model for the ZBOX kiosk bridge (/api/kiosk/summary), shared with Kreader.
// Pure C++ (no LVGL, no network) so it compiles unchanged in the ESP-IDF firmware.
#pragma once
#include <string>
#include <vector>

namespace home {

enum class DeviceType { Light, Ac, Heater, Curtain, Switch };

struct Device {
    std::string id;    // Home Assistant entity_id, e.g. "switch.den_bep_switch_2"
    std::string name;
    std::string room;  // only set on quick cards
    std::string sub;
    DeviceType type = DeviceType::Switch;
    bool on = false;   // curtain: true = open
    int temp = 0;      // ac only
    std::string mode;  // ac only
};

struct Room {
    std::string key;
    std::string title;
    std::vector<Device> devices;
};

struct Summary {
    bool has_env = false;
    double indoor_temp = 0, indoor_humid = 0, outdoor_temp = 0;
    std::string outdoor_desc;
    std::vector<Device> quick;
    std::vector<Room> rooms;
};

// Returns false (and sets err) when the JSON is not a kiosk summary.
bool ParseSummary(const std::string& json, Summary& out, std::string& err);

// A Home Assistant service call understood by the bridge's /api/ha/call.
struct Command {
    std::string domain, service, entity_id;
    int temperature = -1;      // climate.set_temperature
    std::string hvac_mode;     // climate.set_hvac_mode
};

// Same mapping Kreader's HomeCenterActivity uses for a tap on a device card.
Command ToggleCommand(const Device& d);
Command AcTempCommand(const Device& d, int target);
std::string CommandJson(const Command& c);

const char* TypeLabel(DeviceType t);
std::string StateLabel(const Device& d);

}  // namespace home
