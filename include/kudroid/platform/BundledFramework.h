// Loading the frameworks KuDroid ships in its own bundle (ANGLE, MoltenVK).
//
// The naive `dlopen("@executable_path/Frameworks/...")` is wrong in one important
// deployment: LiveContainer runs a guest app as a DYLIB inside its own process, so
// dyld's main executable is LiveContainer's binary and `@executable_path` points at
// LiveContainer's directory, not at the guest .app that actually contains the
// frameworks. LiveContainer patches dyld's recorded executable path to compensate,
// but that patch depends on per-iOS-version struct offsets and does not apply in
// every mode — when it misses, every GL and Vulkan entry point resolves to null and
// the app renders nothing, with no message that names the cause.
//
// `@loader_path` needs no such cooperation: dyld expands it relative to the IMAGE
// that contains the calling code, which is KuDroid's own binary wherever that binary
// lives. It is therefore tried first, and is also correct for an ordinary install
// (where the loader IS the main executable, so the two are the same directory).
#ifndef KUDROID_PLATFORM_BUNDLEDFRAMEWORK_H
#define KUDROID_PLATFORM_BUNDLEDFRAMEWORK_H

namespace kudroid {

// dlopen a framework from KuDroid's own bundle.
//
// `relativePath` is the path under Frameworks/, e.g.
// "libEGL.framework/libEGL". Returns the handle or nullptr; on failure every
// attempted path has been logged, because "GL entry point not found" on its own does
// not say whether the framework was missing, unreadable or merely looked for in the
// wrong place.
//
// `flags` are passed to dlopen unchanged. The result is NOT cached here — callers
// keep their own static handle, and caching in two places would hide which one won.
void* dlopen_bundled_framework(const char* relativePath, int flags);

}  // namespace kudroid

#endif  // KUDROID_PLATFORM_BUNDLEDFRAMEWORK_H
