#include "kudroid/platform/EglCapabilities.h"
#include "kudroid/platform/GraphicsShim.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace kudroid {

const char* egl_backend_name() {
#if defined(KUDROID_GLES_USE_VULKAN) && KUDROID_GLES_USE_VULKAN
    return "vulkan";
#else
    return "metal";
#endif
}

void* egl_default_display() {
#if defined(KUDROID_GLES_USE_VULKAN) && KUDROID_GLES_USE_VULKAN
    const auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        get_egl_func("eglGetPlatformDisplayEXT"));
    const EGLint attributes[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE,
                                EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE, EGL_NONE};
    return getDisplay ? getDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, attributes)
                      : EGL_NO_DISPLAY;
#else
    const auto getDisplay = reinterpret_cast<decltype(&eglGetDisplay)>(get_egl_func("eglGetDisplay"));
    return getDisplay ? getDisplay(EGL_DEFAULT_DISPLAY) : EGL_NO_DISPLAY;
#endif
}

namespace {
constexpr std::string_view recordable = "EGL_ANDROID_recordable";
// Timer query results never become available on the bundled backend; engines that
// wait for a free query slot spin forever once the ring fills.
constexpr std::string_view hiddenGl[] = {"GL_EXT_disjoint_timer_query"};

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

// Interned strings remain valid across queries and concurrent callers.
template <typename Drop>
const char* without_tokens(std::string_view source, Drop drop) {
    std::string filtered;
    for (size_t start = 0; start < source.size();) {
        const size_t end = source.find(' ', start);
        const auto token = source.substr(start, end == std::string_view::npos ? end : end - start);
        if (!token.empty() && !drop(token)) {
            if (!filtered.empty()) filtered += ' ';
            filtered.append(token);
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    static std::mutex mutex;
    static std::set<std::string> strings;
    std::lock_guard<std::mutex> lock(mutex);
    return strings.insert(std::move(filtered)).first->c_str();
}

bool gl_hidden(std::string_view token) {
    for (auto hidden : hiddenGl) if (token == hidden) return true;
    return false;
}
}

bool gl_extension_hidden(const char* name) {
    return name && gl_hidden(name);
}

const char* gl_context_extensions(const char* extensions) {
    if (!extensions) return extensions;
    bool present = false;
    for (auto hidden : hiddenGl) present |= has_token(extensions, hidden);
    return present ? without_tokens(extensions, gl_hidden) : extensions;
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

    return without_tokens(extensions, [](std::string_view token) { return token == recordable; });
}
}
