// Online libraries: public Google Drive folders the owner adds (name + link). Stored on the SD
// card (<sd>/.metalio/libraries.json), never in the firmware or the repository. Added on the device
// or from a phone through the Wi-Fi transfer page.
#pragma once
#include <string>
#include <vector>

namespace online {

struct Source {
    std::string name;
    std::string link;
};

std::vector<Source> Sources();          // saved ones; falls back to the configured link
bool Add(const std::string& name, const std::string& link, std::string& err);
bool Remove(size_t index);
bool Rename(size_t index, const std::string& name);

}  // namespace online
