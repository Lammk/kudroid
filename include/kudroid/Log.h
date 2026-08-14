#pragma once

// Pipeline log chuẩn của KuDroid — MỘT nơi duy nhất định nghĩa cách log:
// ghi ra stdout ([AndroidLog][tag]: ...), file kudroid_android_logs.txt và
// crash buffer (kudroid_crash.log "log up to crash"). Mọi shim/core dùng KLOG
// thay vì tự fprintf/logAndroidMessage rải rác.
//
// Level khớp Android log priorities (android/log.h).

namespace kudroid {
namespace log {

enum Level {
    kDebug = 2,  // verbose per-call (dlsym, egl forward, ...)
    kInfo  = 4,
    kWarn  = 5,
    kError = 6,
};

// Gate verbose: mặc định BẬT (đang debug máy thật). Tắt bằng env
// KUDROID_LOG_VERBOSE=0 hoặc kudroid_log_set_verbose(false).
void set_verbose(bool enabled);
bool verbose_enabled();

// Ghi qua pipeline chuẩn (stdout + file + crash buffer).
void write(Level level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

} // namespace log

// Rút gọn dùng trong code đã nằm trong namespace kudroid (vd shims):
// KLOG(kDebug, ...) thay vì KLOG(kudroid::log::kDebug, ...).
using log::kDebug;
using log::kInfo;
using log::kWarn;
using log::kError;

} // namespace kudroid

// Luôn ghi theo level (bất kể verbose).
#define KLOG(level, tag, ...) \
    ::kudroid::log::write((level), (tag), __VA_ARGS__)

// Chỉ ghi khi verbose bật — dùng cho log per-call dễ nuốt file log
// (dlsym resolution, egl forward từng hàm...).
#define KLOGV(tag, ...)                                                     \
    do {                                                                    \
        if (::kudroid::log::verbose_enabled()) {                            \
            ::kudroid::log::write(::kudroid::log::kDebug, (tag), __VA_ARGS__); \
        }                                                                   \
    } while (0)
