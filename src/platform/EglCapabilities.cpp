#include "kudroid/platform/EglCapabilities.h"
#include "kudroid/platform/GraphicsShim.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace kudroid {
namespace {
constexpr std::string_view recordable = "EGL_ANDROID_recordable";

bool has_token(std::string_view extensions, std::string_view token) {
    size_t position = 0;
    while ((position = extensions.find(token, position)) != std::string_view::npos) {
        const size_t end = position + token.size();
        if ((position == 0 || extensions[position - 1] == ' ') &&
            (end == extensions.size() || extensions[end] == ' ')) return true;
        position = end;
    }
    return false;
}
}

const char* egl_display_extensions(void* display, const char* extensions) {
    if (!display || !extensions || !has_token(extensions, recordable)) return extensions;
    const auto getConfigs = reinterpret_cast<decltype(&eglGetConfigs)>(get_egl_func("eglGetConfigs"));
    const auto getAttribute = reinterpret_cast<decltype(&eglGetConfigAttrib)>(get_egl_func("eglGetConfigAttrib"));
    if (!getConfigs || !getAttribute) return extensions;

    // Valid config queries leave the caller's pending EGL error untouched.
    EGLint count = 0;
    if (!getConfigs(display, nullptr, 0, &count) || count < 0) return extensions;
    if (count > 0) {
        std::vector<EGLConfig> configs(static_cast<size_t>(count));
        EGLint returned = 0;
        if (!getConfigs(display, configs.data(), count, &returned) || returned < 0 || returned > count)
            return extensions;
        for (EGLint i = 0; i < returned; ++i) {
            EGLint supported = EGL_FALSE;
            if (!getAttribute(display, configs[i], EGL_RECORDABLE_ANDROID, &supported)) return extensions;
            if (supported == EGL_TRUE) return extensions;
        }
    }

    std::string filtered;
    std::string_view source(extensions);
    for (size_t start = 0; start < source.size();) {
        const size_t end = source.find(' ', start);
        const auto token = source.substr(start, end == std::string_view::npos ? end : end - start);
        if (!token.empty() && token != recordable) {
            if (!filtered.empty()) filtered += ' ';
            filtered.append(token);
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    // Interned strings remain valid across display queries and concurrent callers.
    static std::mutex mutex;
    static std::set<std::string> strings;
    std::lock_guard<std::mutex> lock(mutex);
    return strings.insert(std::move(filtered)).first->c_str();
}
}
