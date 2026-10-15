#ifndef KUDROID_ANDROID_BASE_PARSEINT_H_
#define KUDROID_ANDROID_BASE_PARSEINT_H_

#include <errno.h>
#include <stdlib.h>

#include <limits>
#include <string>

namespace android {
namespace base {

template <typename T>
bool ParseUint(const char* s, T* out, T max = std::numeric_limits<T>::max(),
               bool allow_suffixes = false) {
    while (*s == ' ') ++s;
    if (*s == '-') return false;

    errno = 0;
    char* end = nullptr;
    unsigned long long int result = strtoull(s, &end, 0);
    if (errno != 0 || s == end || (!allow_suffixes && *end != '\0')) return false;
    if (result > max) return false;
    if (out != nullptr) *out = static_cast<T>(result);
    return true;
}

template <typename T>
bool ParseInt(const char* s, T* out, T min = std::numeric_limits<T>::min(),
              T max = std::numeric_limits<T>::max()) {
    while (*s == ' ') ++s;

    errno = 0;
    char* end = nullptr;
    long long int result = strtoll(s, &end, 0);
    if (errno != 0 || s == end || *end != '\0') return false;
    if (result < min || max < result) return false;
    if (out != nullptr) *out = static_cast<T>(result);
    return true;
}

template <typename T>
bool ParseInt(const std::string& s, T* out, T min = std::numeric_limits<T>::min(),
              T max = std::numeric_limits<T>::max()) {
    return ParseInt(s.c_str(), out, min, max);
}

template <typename T>
bool ParseUint(const std::string& s, T* out, T max = std::numeric_limits<T>::max(),
               bool allow_suffixes = false) {
    return ParseUint(s.c_str(), out, max, allow_suffixes);
}

}  // namespace base
}  // namespace android

#endif  // KUDROID_ANDROID_BASE_PARSEINT_H_
