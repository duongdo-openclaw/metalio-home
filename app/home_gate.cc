#include "home_gate.h"

#include <cstdio>

namespace home {

std::string UrlHost(const std::string& url) {
    size_t s = url.find("://");
    size_t a = s == std::string::npos ? 0 : s + 3;
    size_t e = url.find_first_of(":/?#", a);
    return url.substr(a, e == std::string::npos ? std::string::npos : e - a);
}

bool IsPrivateIpv4(const std::string& host, bool allow_loopback) {
    unsigned a, b, c, d;
    char tail;
    if (std::sscanf(host.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    if (a == 10) return true;
    if (a == 172 && b >= 16 && b <= 31) return true;
    if (a == 192 && b == 168) return true;
    if (a == 169 && b == 254) return true;
    return allow_loopback && a == 127;
}

}  // namespace home
