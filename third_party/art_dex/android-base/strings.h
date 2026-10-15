#ifndef KUDROID_ANDROID_BASE_STRINGS_H_
#define KUDROID_ANDROID_BASE_STRINGS_H_

#include <string>
#include <vector>

namespace android {
namespace base {

std::vector<std::string> Split(const std::string& s, const std::string& delimiters);
std::string Trim(const std::string& s);
bool StartsWith(std::string_view s, std::string_view prefix);
bool EndsWith(std::string_view s, std::string_view suffix);

template <typename ContainerT, typename SeparatorT>
std::string Join(const ContainerT& things, SeparatorT separator) {
    if (things.empty()) return "";
    std::string result;
    for (const auto& thing : things) {
        if (!result.empty()) result += separator;
        result += thing;
    }
    return result;
}

}  // namespace base
}  // namespace android

#endif  // KUDROID_ANDROID_BASE_STRINGS_H_
