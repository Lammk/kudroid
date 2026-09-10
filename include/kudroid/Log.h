#pragma once

// KuDroid's standard log pipeline — ONE place that defines how to log:
// write to stdout ([AndroidLog][tag]: ...), file kudroid_android_logs.txt and
// crash buffer (kudroid_crash.log "log up to crash"). All shims/cores use KLOG
// instead of scattered fprintf/logAndroidMessage itself.
//
// Level matches Android log priorities (android/log.h).

namespace kudroid {
namespace log {

enum Level {
    kDebug = 2,  // verbose per-call (dlsym, egl forward, ...)
    kInfo  = 4,
    kWarn  = 5,
    kError = 6,
};

// Gate verbose: default ON (debugging real machine). Turn off using env
// KUDROID_LOG_VERBOSE=0 or kudroid_log_set_verbose(false).
void set_verbose(bool enabled);
bool verbose_enabled();

// Gate the per-JNI-call enter/exit lines (KuARTNative, FMOD, JNIBridge, ...).
// One line pair per call at 60fps is thousands of lines a second — the log
// pipeline itself was a visible fraction of frame cost. Default matches
// verbose: on only for KUDROID_LOG_JNI=1 or kudroid_log_set_jni(true).
void set_jni(bool enabled);
bool jni_enabled();

// Write via standard pipeline (stdout + file + crash buffer).
void write(Level level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

} // namespace log

// Shortening used in code already in the kudroid namespace (eg shims):
// KLOG(kDebug, ...) instead of KLOG(kudroid::log::kDebug, ...).
using log::kDebug;
using log::kInfo;
using log::kWarn;
using log::kError;

} // namespace kudroid

// Always write by level (regardless of verbose).
#define KLOG(level, tag, ...) \
    ::kudroid::log::write((level), (tag), __VA_ARGS__)

// Only log when verbose is on — used for per-call logging that easily swallows the log file
// (dlsym resolution, egl forward each function...).
#define KLOGV(tag, ...)                                                     \
    do {                                                                    \
        if (::kudroid::log::verbose_enabled()) {                            \
            ::kudroid::log::write(::kudroid::log::kDebug, (tag), __VA_ARGS__); \
        }                                                                   \
    } while (0)

// Per-JNI-call lines: verbose-gated AND jni-gated. The second gate exists so
// verbose per-call logs elsewhere (dlsym, EGL) survive without the multi-kHz
// enter/exit stream from the JNI boundary.
#define KLOGJNI(tag, ...)                                                   \
    do {                                                                    \
        if (::kudroid::log::verbose_enabled() &&                            \
            ::kudroid::log::jni_enabled()) {                                \
            ::kudroid::log::write(::kudroid::log::kDebug, (tag), __VA_ARGS__); \
        }                                                                   \
    } while (0)
