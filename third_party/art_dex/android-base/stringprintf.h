#ifndef KUDROID_ANDROID_BASE_STRINGPRINTF_H_
#define KUDROID_ANDROID_BASE_STRINGPRINTF_H_

#include <stdarg.h>
#include <string>

namespace android {
namespace base {

std::string StringPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void StringAppendF(std::string* dst, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
void StringAppendV(std::string* dst, const char* format, va_list ap);

}  // namespace base
}  // namespace android

#endif  // KUDROID_ANDROID_BASE_STRINGPRINTF_H_
