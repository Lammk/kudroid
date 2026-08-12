// Verification: EGL symbol coverage, __system_property_read, __FD_ISSET_chk.
// Build like test_dex_to_jar: link against kudroid_core.
#include "kudroid/BionicShim.h"
#include "kudroid/shims/GraphicsShim.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// kudroid_core.a pulls in SyscallShim.o + kudroid_jni.o transitively when we
// reference resolve_bionic_symbol. Those reference JNI_CreateJavaVM (Avian,
// not built on Linux) and __error (macOS errno hook). The test never calls
// them — provide weak stubs so the archive links on Linux.
extern "C" {
__attribute__((weak)) int* __error(void) { static int e = 0; return &e; }
__attribute__((weak)) int JNI_CreateJavaVM(void**, void**, void*) { return -1; }
}

typedef int (*PropGetFn)(const char*, char*);
typedef const void* (*PropFindFn)(const char*);
typedef int (*PropReadFn)(const void*, char*, char*);
typedef void (*PropReadCbFn)(void*, void (*cb)(void*, const char*, const char*, unsigned), void*);
typedef int (*FdIssetChkFn)(int, const void*, size_t);
typedef void (*FdSetChkFn)(int, void*, size_t);

extern "C" void kudroid_register_guest_module(void*, std::size_t, const char*);
extern "C" bool kudroid_lookup_guest_module(void*, char*, std::size_t);

int failures = 0;
static void check(bool cond, const char* what) {
    if (cond) {
        std::printf("  PASS: %s\n", what);
    } else {
        std::printf("  FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    std::printf("=== EGL symbol coverage ===\n");
    const char* eglSymbols[] = {
        "eglGetDisplay", "eglGetPlatformDisplayEXT", "eglGetPlatformDisplay",
        "eglGetProcAddress", "eglCreateWindowSurface", "eglInitialize",
        "eglTerminate", "eglChooseConfig", "eglGetConfigAttrib", "eglGetConfigs",
        "eglCreateContext", "eglDestroyContext", "eglCreatePbufferSurface",
        "eglDestroySurface", "eglMakeCurrent", "eglGetCurrentContext",
        "eglGetCurrentSurface", "eglGetCurrentDisplay", "eglSwapBuffers",
        "eglSwapInterval", "eglQueryString", "eglQuerySurface", "eglGetError",
        "eglSurfaceAttrib", "eglReleaseThread", "eglBindAPI", "eglQueryAPI",
        "eglBindTexImage", "eglReleaseTexImage", "eglWaitGL", "eglWaitNative",
    };
    size_t count = 0;
    const kudroid::SymbolEntry* table = kudroid::get_graphics_symbols(&count);
    int covered = 0;
    for (const char* sym : eglSymbols) {
        bool found = false;
        for (size_t i = 0; i < count; ++i) {
            if (std::strcmp(table[i].name, sym) == 0) { found = true; break; }
        }
        if (found) covered++;
        else std::printf("  MISSING from table: %s\n", sym);
    }
    check(covered == 31, "all 31 EGL symbols present in graphics table");

    // The exact symbols from the crash log must be covered
    const char* crashSymbols[] = {
        "eglQuerySurface", "eglChooseConfig", "eglSwapBuffers",
        "eglGetCurrentContext", "eglTerminate", "eglQueryString",
        "eglCreateContext", "eglMakeCurrent", "eglGetConfigAttrib",
        "eglDestroyContext", "eglGetError", "eglSurfaceAttrib",
        "eglSwapInterval", "eglCreatePbufferSurface", "eglDestroySurface",
        "eglGetCurrentSurface", "eglInitialize",
    };
    int crashCovered = 0;
    for (const char* sym : crashSymbols) {
        bool found = false;
        for (size_t i = 0; i < count; ++i) {
            if (std::strcmp(table[i].name, sym) == 0) { found = true; break; }
        }
        if (found) crashCovered++;
    }
    check(crashCovered == 17, "all 17 crash-log egl symbols covered");

    std::printf("=== resolve_bionic_symbol not-dummy ===\n");
    void* dummy = kudroid::resolve_bionic_symbol("__no_such_symbol_kudroid_xyz__");
    void* eglInit = kudroid::resolve_bionic_symbol("eglInitialize");
    void* eglSwap = kudroid::resolve_bionic_symbol("eglSwapBuffers");
    void* eglQStr = kudroid::resolve_bionic_symbol("eglQueryString");
    check(dummy != nullptr, "missing symbol resolves to dummy (non-null)");
    check(eglInit != nullptr && eglInit != dummy, "eglInitialize resolves to real shim, not dummy");
    check(eglSwap != nullptr && eglSwap != dummy, "eglSwapBuffers resolves to real shim, not dummy");
    check(eglQStr != nullptr && eglQStr != dummy, "eglQueryString resolves to real shim, not dummy");

    std::printf("=== __system_property_read ===\n");
    auto getFn = (PropGetFn)kudroid::resolve_bionic_symbol("__system_property_get");
    auto findFn = (PropFindFn)kudroid::resolve_bionic_symbol("__system_property_find");
    auto readFn = (PropReadFn)kudroid::resolve_bionic_symbol("__system_property_read");
    auto readCbFn = (PropReadCbFn)kudroid::resolve_bionic_symbol("__system_property_read_callback");
    check(getFn && findFn && readFn && readCbFn, "all 4 property symbols resolve");
    check(getFn && getFn != dummy, "__system_property_get not dummy");

    char buf[64];
    int n = getFn("ro.build.version.sdk", buf);
    check(n == 2 && std::strcmp(buf, "29") == 0, "property_get ro.build.version.sdk == 29");

    const void* pi = findFn("ro.build.version.sdk");
    check(pi != nullptr, "property_find returns prop_info for known key");
    char name[64] = {0}, value[64] = {0};
    int rn = readFn(pi, name, value);
    check(rn == 2 && std::strcmp(name, "ro.build.version.sdk") == 0 && std::strcmp(value, "29") == 0,
          "property_read returns name+value correctly");
    check(findFn("no.such.key") == nullptr, "property_find unknown key -> nullptr");
    check(readFn(nullptr, name, value) == 0, "property_read nullptr pi -> 0");

    bool cbCalled = false;
    auto cb = [](void* cookie, const char* n, const char* v, unsigned) {
        bool* flag = static_cast<bool*>(cookie);
        if (n && v && std::strcmp(n, "ro.build.version.release") == 0 && std::strcmp(v, "10") == 0)
            *flag = true;
    };
    readCbFn((void*)findFn("ro.build.version.release"), cb, &cbCalled);
    check(cbCalled, "read_callback fires with correct name/value");

    std::printf("=== __FD_ISSET_chk / __FD_SET_chk ===\n");
    auto issetFn = (FdIssetChkFn)kudroid::resolve_bionic_symbol("__FD_ISSET_chk");
    auto setFn = (FdSetChkFn)kudroid::resolve_bionic_symbol("__FD_SET_chk");
    check(issetFn && setFn, "FD_ISSET_chk + FD_SET_chk resolve");
    unsigned char set[32] = {0};
    setFn(5, set, sizeof(set));
    check(issetFn(5, set, sizeof(set)) == 1, "FD_ISSET_chk sees fd 5 after FD_SET_chk");
    check(issetFn(7, set, sizeof(set)) == 0, "FD_ISSET_chk fd 7 not set");
    check(issetFn(500, set, sizeof(set)) == 0, "FD_ISSET_chk out-of-range fd -> 0 (no crash)");
    check(issetFn(-1, set, sizeof(set)) == 0, "FD_ISSET_chk negative fd -> 0");

    std::printf("=== guest module registry ===\n");
    void* modBase = (void*)0x40000000;   // fake guest base
    kudroid_register_guest_module(modBase, 0x10000, "/data/app/com.foo/lib/arm64/libunity.so");
    char sym[512] = {0};
    check(kudroid_lookup_guest_module((void*)0x40001234, sym, sizeof(sym)),
          "lookup finds address inside registered range");
    check(std::strstr(sym, "libunity.so+0x1234") != nullptr,
          "lookup renders path+offset");
    check(!kudroid_lookup_guest_module((void*)0x50001234, sym, sizeof(sym)),
          "lookup misses address outside range");
    kudroid_register_guest_module(modBase, 0x20000, "/data/app/com.foo/lib/arm64/libunity.so");  // update
    check(kudroid_lookup_guest_module((void*)0x4001abcd, sym, sizeof(sym)),
          "re-register same base updates range");
    check(kudroid_lookup_guest_module(nullptr, sym, sizeof(sym)) == false,
          "lookup(nullptr) -> false");

    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
