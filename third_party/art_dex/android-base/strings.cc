#include "android-base/strings.h"

#include <string.h>

namespace android {
namespace base {

std::vector<std::string> Split(const std::string& s, const std::string& delimiters) {
    std::vector<std::string> result;
    if (delimiters.empty()) {
        result.push_back(s);
        return result;
    }
    size_t base = 0;
    size_t found;
    while (true) {
        found = s.find_first_of(delimiters, base);
        result.push_back(s.substr(base, found - base));
        if (found == std::string::npos) break;
        base = found + 1;
    }
    return result;
}

std::string Trim(const std::string& s) {
    std::string result;
    if (s.size() == 0) return result;
    size_t start_index = 0;
    size_t end_index = s.size() - 1;

    while (start_index < s.size() && isspace(s[start_index])) ++start_index;
    while (end_index > start_index && isspace(s[end_index])) --end_index;

    if (end_index >= start_index) {
        result = s.substr(start_index, end_index - start_index + 1);
    }
    return result;
}

bool StartsWith(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size(), suffix.size()) == suffix;
}

}  // namespace base
}  // namespace android
