#include "kiosk_model.h"

#include <cJSON.h>

#include <cstdio>
#include <memory>

namespace home {
namespace {

std::string Str(const cJSON* o, const char* k, const char* def = "") {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : def;
}
double Num(const cJSON* o, const char* k, double def = 0) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}
bool Bool(const cJSON* o, const char* k) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsTrue(v);
}

DeviceType ParseType(const std::string& t, const std::string& id) {
    if (t == "light") return DeviceType::Light;
    if (t == "ac") return DeviceType::Ac;
    if (t == "heater") return DeviceType::Heater;
    if (t == "curtain") return DeviceType::Curtain;
    if (id.rfind("climate.", 0) == 0) return DeviceType::Ac;
    if (id.rfind("cover.", 0) == 0) return DeviceType::Curtain;
    return DeviceType::Switch;
}

bool ParseDevice(const cJSON* o, Device& d) {
    if (!cJSON_IsObject(o)) return false;
    d.id = Str(o, "id");
    if (d.id.empty()) return false;
    d.name = Str(o, "name", d.id.c_str());
    d.room = Str(o, "room");
    d.sub = Str(o, "sub");
    d.type = ParseType(Str(o, "type"), d.id);
    d.on = Bool(o, "state");
    d.temp = (int)Num(o, "temp", 0);
    d.mode = Str(o, "mode");
    return true;
}

void ParseDevices(const cJSON* arr, std::vector<Device>& out) {
    const cJSON* it = nullptr;
    cJSON_ArrayForEach(it, arr) {
        Device d;
        if (ParseDevice(it, d)) out.push_back(d);
    }
}

}  // namespace

bool ParseSummary(const std::string& json, Summary& out, std::string& err) {
    std::unique_ptr<cJSON, void (*)(cJSON*)> root(cJSON_ParseWithLength(json.data(), json.size()),
                                                  cJSON_Delete);
    if (!root || !cJSON_IsObject(root.get())) {
        err = "JSON không hợp lệ";
        return false;
    }
    out = Summary{};
    const cJSON* env = cJSON_GetObjectItemCaseSensitive(root.get(), "environment");
    if (cJSON_IsObject(env)) {
        out.has_env = true;
        out.indoor_temp = Num(env, "indoor_temp");
        out.indoor_humid = Num(env, "indoor_humid");
        out.outdoor_temp = Num(env, "outdoor_temp");
        out.outdoor_desc = Str(env, "outdoor_desc");
    }
    ParseDevices(cJSON_GetObjectItemCaseSensitive(root.get(), "quick_cards"), out.quick);
    const cJSON* rooms = cJSON_GetObjectItemCaseSensitive(root.get(), "rooms");
    const cJSON* r = nullptr;
    cJSON_ArrayForEach(r, rooms) {
        if (!cJSON_IsObject(r) || !r->string) continue;
        Room room;
        room.key = r->string;
        room.title = Str(r, "title", r->string);
        ParseDevices(cJSON_GetObjectItemCaseSensitive(r, "devices"), room.devices);
        out.rooms.push_back(room);
    }
    if (!out.has_env && out.quick.empty() && out.rooms.empty()) {
        err = "Không phải dữ liệu kiosk";
        return false;
    }
    return true;
}

Command ToggleCommand(const Device& d) {
    Command c;
    c.entity_id = d.id;
    switch (d.type) {
    case DeviceType::Curtain:
        c.domain = "cover";
        c.service = d.on ? "close_cover" : "open_cover";
        break;
    case DeviceType::Ac:
        c.domain = "climate";
        c.service = "set_hvac_mode";
        c.hvac_mode = d.on ? "off" : (d.mode.empty() ? "cool" : d.mode);
        break;
    default:
        c.domain = "switch";
        c.service = "toggle";
        break;
    }
    return c;
}

Command AcTempCommand(const Device& d, int target) {
    Command c;
    c.domain = "climate";
    c.service = "set_temperature";
    c.entity_id = d.id;
    c.temperature = target;
    return c;
}

std::string CommandJson(const Command& c) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "domain", c.domain.c_str());
    cJSON_AddStringToObject(root, "service", c.service.c_str());
    cJSON* data = cJSON_AddObjectToObject(root, "service_data");
    cJSON_AddStringToObject(data, "entity_id", c.entity_id.c_str());
    if (c.temperature >= 0) cJSON_AddNumberToObject(data, "temperature", c.temperature);
    if (!c.hvac_mode.empty()) cJSON_AddStringToObject(data, "hvac_mode", c.hvac_mode.c_str());
    char* s = cJSON_PrintUnformatted(root);
    std::string out = s ? s : "";
    cJSON_free(s);
    cJSON_Delete(root);
    return out;
}

const char* TypeLabel(DeviceType t) {
    switch (t) {
    case DeviceType::Light: return "Đèn";
    case DeviceType::Ac: return "Điều hoà";
    case DeviceType::Heater: return "Nóng lạnh";
    case DeviceType::Curtain: return "Rèm";
    default: return "Công tắc";
    }
}

std::string StateLabel(const Device& d) {
    if (d.type == DeviceType::Curtain) return d.on ? "ĐANG MỞ" : "ĐANG ĐÓNG";
    if (d.type == DeviceType::Ac) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%s · %d°C", d.on ? "BẬT" : "TẮT", d.temp);
        return buf;
    }
    return d.on ? "BẬT" : "TẮT";
}

}  // namespace home
