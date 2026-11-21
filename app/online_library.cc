#include "online_library.h"

#include <cJSON.h>

#include "drive_folder.h"
#include "platform.h"

namespace online {
namespace {

std::string File() { return platform::StateDir() + "/libraries.json"; }

std::vector<Source> Load(bool& exists) {
    std::vector<Source> out;
    std::string s;
    exists = platform::ReadText(File(), s);
    if (!exists) return out;
    cJSON* root = cJSON_Parse(s.c_str());
    const cJSON* it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItem(root, "libraries")) {
        const cJSON* n = cJSON_GetObjectItem(it, "name");
        const cJSON* l = cJSON_GetObjectItem(it, "link");
        if (cJSON_IsString(l)) out.push_back({cJSON_IsString(n) ? n->valuestring : "Thư viện", l->valuestring});
    }
    cJSON_Delete(root);
    return out;
}

bool Save(const std::vector<Source>& v) {
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "libraries");
    for (const auto& s : v) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", s.name.c_str());
        cJSON_AddStringToObject(o, "link", s.link.c_str());
        cJSON_AddItemToArray(arr, o);
    }
    char* out = cJSON_Print(root);
    bool ok = out && platform::WriteText(File(), out);
    cJSON_free(out);
    cJSON_Delete(root);
    return ok;
}

}  // namespace

std::vector<Source> Sources() {
    bool exists = false;
    auto v = Load(exists);
    if (!exists && !platform::GetConfig().drive_folder.empty())
        v.push_back({"Thư viện của tôi", platform::GetConfig().drive_folder});   // first run: configured link
    return v;
}

bool Add(const std::string& name, const std::string& link, std::string& err) {
    std::string l = link;
    while (!l.empty() && (l.back() == ' ' || l.back() == '\n' || l.back() == '\r')) l.pop_back();
    while (!l.empty() && l.front() == ' ') l.erase(0, 1);
    if (drive::ParseLink(l).empty() || l.size() < 10) { err = "Link không phải thư mục Google Drive"; return false; }
    auto v = Sources();
    for (const auto& s : v) if (drive::ParseLink(s.link).id == drive::ParseLink(l).id) { err = "Thư viện này đã có"; return false; }
    v.push_back({name.empty() ? "Thư viện " + std::to_string(v.size() + 1) : name, l});
    if (!Save(v)) { err = "Không ghi được thẻ nhớ"; return false; }
    return true;
}

bool Remove(size_t i) {
    auto v = Sources();
    if (i >= v.size()) return false;
    v.erase(v.begin() + (long)i);
    return Save(v);
}

bool Rename(size_t i, const std::string& name) {
    auto v = Sources();
    if (i >= v.size() || name.empty()) return false;
    v[i].name = name;
    return Save(v);
}

}  // namespace online
