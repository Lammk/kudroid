#include "android-base/logging.h"

#include <stdio.h>
#include <string.h>

namespace android {
namespace base {

namespace {

LogLineHook g_hook = nullptr;
LogSeverity g_min_severity = INFO;

const char* SeverityName(LogSeverity severity) {
    switch (severity) {
        case VERBOSE: return "V";
        case DEBUG: return "D";
        case INFO: return "I";
        case WARNING: return "W";
        case ERROR: return "E";
        case FATAL_WITHOUT_ABORT: return "F";
        case FATAL: return "F";
    }
    return "?";
}

// Chỉ lấy tên file, bỏ đường dẫn — log ngắn và không lộ đường dẫn build.
const char* Basename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

}  // namespace

void SetLogLineHook(LogLineHook hook) { g_hook = hook; }

void SetMinimumLogSeverity(LogSeverity severity) { g_min_severity = severity; }

LogSeverity GetMinimumLogSeverity() { return g_min_severity; }

bool ShouldLog(LogSeverity severity) { return severity >= g_min_severity; }

void StderrLogger(LogSeverity severity, const char* msg) {
    fprintf(stderr, "%s %s\n", SeverityName(severity), msg);
    fflush(stderr);
}

void LogdLogger(LogSeverity severity, const char* msg) { StderrLogger(severity, msg); }

void InitLogging(char** /*argv*/, LogLineHook hook) { g_hook = hook; }

LogMessage::LogMessage(const char* file, unsigned int line, LogSeverity severity, int error)
    : file_(file), line_(line), severity_(severity), error_(error) {}

LogMessage::~LogMessage() {
    if (error_ != -1) {
        stream_ << ": " << strerror(error_);
    }

    std::string body = stream_.str();
    char header[256];
    snprintf(header, sizeof(header), "[art_dex] %s:%u: ", Basename(file_), line_);
    std::string line = header;
    line += body;

    if (g_hook != nullptr) {
        g_hook(severity_, line.c_str());
    } else {
        StderrLogger(severity_, line.c_str());
    }

    if (severity_ == FATAL) {
        abort();
    }
}

}  // namespace base
}  // namespace android
