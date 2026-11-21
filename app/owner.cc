#include "owner.h"

#include <cJSON.h>

#include "platform.h"

namespace owner {
namespace {
std::string File() { return platform::StateDir() + "/owner.json"; }
std::string Str(const cJSON* o, const char* k) {
    const cJSON* v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
}  // namespace

Info Get() {
    Info i;
    std::string s;
    if (!platform::ReadText(File(), s)) return i;
    cJSON* root = cJSON_Parse(s.c_str());
    if (cJSON_IsObject(root)) {
        i.name = Str(root, "name");
        i.contact = Str(root, "contact");
        i.message = Str(root, "message");
    }
    cJSON_Delete(root);
    return i;
}

bool Save(const Info& i) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", i.name.c_str());
    cJSON_AddStringToObject(root, "contact", i.contact.c_str());
    cJSON_AddStringToObject(root, "message", i.message.c_str());
    char* s = cJSON_Print(root);
    bool ok = s && platform::WriteText(File(), s);
    cJSON_free(s);
    cJSON_Delete(root);
    return ok;
}

std::string FirstName(const Info& i) {
    std::string n = i.name;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    size_t sp = n.rfind(' ');
    return sp == std::string::npos ? n : n.substr(sp + 1);
}

}  // namespace owner
