#include "android-base/stringprintf.h"

#include <stdio.h>

namespace android {
namespace base {

void StringAppendV(std::string* dst, const char* format, va_list ap) {
    // Thử vào buffer stack trước — đa số format string ngắn hơn 1KB.
    char stack_buf[1024];
    va_list backup_ap;
    va_copy(backup_ap, ap);
    int result = vsnprintf(stack_buf, sizeof(stack_buf), format, backup_ap);
    va_end(backup_ap);

    if (result < 0) return;

    if (static_cast<size_t>(result) < sizeof(stack_buf)) {
        dst->append(stack_buf, result);
        return;
    }

    // vsnprintf đã cho biết cần bao nhiêu byte; cấp đúng chừng đó rồi in lại.
    const size_t needed = static_cast<size_t>(result) + 1;
    std::string heap_buf;
    heap_buf.resize(needed);
    va_copy(backup_ap, ap);
    result = vsnprintf(&heap_buf[0], needed, format, backup_ap);
    va_end(backup_ap);

    if (result >= 0 && static_cast<size_t>(result) < needed) {
        dst->append(heap_buf.data(), result);
    }
}

std::string StringPrintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string result;
    StringAppendV(&result, fmt, ap);
    va_end(ap);
    return result;
}

void StringAppendF(std::string* dst, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    StringAppendV(dst, fmt, ap);
    va_end(ap);
}

}  // namespace base
}  // namespace android
