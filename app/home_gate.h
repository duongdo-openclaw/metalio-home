// "Only when at home" gate — port of Kreader's LanProbe/HaCommandGate idea.
// The smart-home screen is usable only when the bridge answers from a private (LAN) address.
#pragma once
#include <string>

namespace home {

// Extracts the host part of an http(s) URL ("http://192.168.1.50:8124/x" -> "192.168.1.50").
std::string UrlHost(const std::string& url);

// True for RFC1918 IPv4 (10/8, 172.16/12, 192.168/16), link-local 169.254/16 and, when
// allow_loopback, 127/8. Hostnames are rejected on purpose: the device must not follow DNS
// to a public address that pretends to be the home bridge.
bool IsPrivateIpv4(const std::string& host, bool allow_loopback);

}  // namespace home
