#include "kudroid/platform/BundledFramework.h"

#include "kudroid/Log.h"

#include <dlfcn.h>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace kudroid {
namespace {

// The directory holding KuDroid's own binary, from dladdr on a function inside it.
//
// This is the fallback for the case where even @loader_path does not apply — dlopen
// on a path containing '/' does not expand @loader_path in some historical dyld
// versions, and an absolute path always works. dladdr reports the image that
// contains the address, which under LiveContainer is the guest's own binary rather
// than LiveContainer's, so this agrees with @loader_path by construction.
std::string ownImageDirectory() {
    Dl_info info = {};
    // The address of a function defined in THIS translation unit, so the image found
    // is the one KuDroid was linked into.
    if (dladdr(reinterpret_cast<const void*>(&ownImageDirectory), &info) == 0 ||
        info.dli_fname == nullptr) {
        return std::string();
    }
    std::string path = info.dli_fname;
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos) return std::string();
    return path.substr(0, slash);
}

}  // namespace

void* dlopen_bundled_framework(const char* relativePath, int flags) {
    if (relativePath == nullptr || *relativePath == '\0') return nullptr;

    std::vector<std::string> candidates;

    // 1. @loader_path — relative to KuDroid's own image. Correct under LiveContainer
    //    without relying on its dyld patch, and identical to @executable_path for a
    //    normal install.
    candidates.emplace_back(std::string("@loader_path/Frameworks/") + relativePath);

    // 2. The same directory as an absolute path, for dyld versions that decline to
    //    expand @loader_path for a dlopen argument.
    const std::string ownDir = ownImageDirectory();
    if (!ownDir.empty()) {
        candidates.emplace_back(ownDir + "/Frameworks/" + relativePath);
    }

    // 3. @executable_path — what this used to try first. Kept because a framework may
    //    legitimately live beside the MAIN executable rather than beside KuDroid's
    //    image, which is the case when KuDroid is embedded in a larger host app.
    candidates.emplace_back(std::string("@executable_path/Frameworks/") + relativePath);

    // 4. The leaf name alone, so an already-loaded image is found by dyld's cache of
    //    install names instead of being mapped a second time.
    candidates.emplace_back(relativePath);

    for (const std::string& candidate : candidates) {
        if (void* handle = ::dlopen(candidate.c_str(), flags)) {
            KLOG(kInfo, "KuDroidGPU", "loaded %s from %s", relativePath, candidate.c_str());
            return handle;
        }
    }

    // Report every path that was tried. "entry point not found" alone does not say
    // whether the framework is absent, unreadable, or was looked for in the wrong
    // place — and under LiveContainer the wrong place is the likely answer.
    KLOG(kError, "KuDroidGPU", "could not load %s; tried %zu paths:", relativePath,
         candidates.size());
    for (const std::string& candidate : candidates) {
        // dlerror() is consumed by each dlopen, so re-attempt to report per-path.
        // Cheap: this only runs on the failure path, once per framework.
        (void)::dlopen(candidate.c_str(), flags);
        const char* why = ::dlerror();
        KLOG(kError, "KuDroidGPU", "  %s -> %s", candidate.c_str(),
             why != nullptr ? why : "(no error reported)");
    }
    return nullptr;
}

}  // namespace kudroid
