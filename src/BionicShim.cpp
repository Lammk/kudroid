#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/abi/SyscallShim.h"
#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/AudioShim.h"
#include "kudroid/platform/AssetShim.h"

#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cxxabi.h>

// Standard log pipeline (stdout + kudroid_android_logs.txt + crash buffer) — definition
// meaning in SyscallShim.cpp, shared with GraphicsShim/kudroid_jni.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

namespace kudroid {
namespace {
thread_local std::string gShimTrace;
} // namespace

void trace_shim(const char* message) {
    if (!message) return;
    gShimTrace += "[BionicShim] ";
    gShimTrace += message;
    gShimTrace += '\n';
}

namespace {
void trace(const char* message) { trace_shim(message); }

extern "C" int kudroid_universal_dummy() {
    trace("universal dummy fallback invoked");
    return 0;
}
} // namespace

bool is_universal_dummy(const void* address) {
    return address == reinterpret_cast<const void*>(&kudroid_universal_dummy);
}

namespace {

std::shared_mutex resolveMutex;
std::unordered_map<std::string, std::pair<void*, bool>> boundSymbols;

void* resolve_from_list(const SymbolEntry* list, size_t count, const char* name) {
    if (!list) return nullptr;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(list[i].name, name) == 0) {
            return list[i].address;
        }
    }
    return nullptr;
}

} // namespace

void print_bound_symbols() {
    trace("Bionic/global binding trace:");
    std::shared_lock<std::shared_mutex> lock(resolveMutex);
    for (const auto& pair : boundSymbols) {
        char msg[256];
        snprintf(msg, sizeof(msg), "bound %s -> %p%s",
                 pair.first.c_str(), pair.second.first, pair.second.second ? " (host)" : "");
        trace(msg);
    }
}

void* resolve_bionic_symbol(const char* name) {
    {
        std::shared_lock<std::shared_mutex> lock(resolveMutex);
        auto it = boundSymbols.find(name);
        if (it != boundSymbols.end()) {
            return it->second.first;
        }
    }

    void* resolved = nullptr;
    bool is_host = false;

    {
        std::unique_lock<std::shared_mutex> lock(resolveMutex);
        auto it = boundSymbols.find(name);
        if (it != boundSymbols.end()) {
            return it->second.first;
        }

        size_t count = 0;
        const SymbolEntry* syscalls = get_syscall_symbols(&count);
        resolved = resolve_from_list(syscalls, count, name);

        if (!resolved) {
            const SymbolEntry* graphics = get_graphics_symbols(&count);
            resolved = resolve_from_list(graphics, count, name);
        }

        if (!resolved) {
            const SymbolEntry* input = get_input_symbols(&count);
            resolved = resolve_from_list(input, count, name);
        }

        if (!resolved) {
            const SymbolEntry* audio = get_audio_symbols(&count);
            resolved = resolve_from_list(audio, count, name);
        }

        if (!resolved) {
            const SymbolEntry* assets = get_asset_symbols(&count);
            resolved = resolve_from_list(assets, count, name);
        }

        // Direct routing for graphics API prefixes (OpenGL ES, EGL, Vulkan)
        if (!resolved && name[0] == 'g' && name[1] == 'l') {
            resolved = get_gl_func(name);
            if (resolved) is_host = true;
        }
        if (!resolved && name[0] == 'e' && name[1] == 'g' && name[2] == 'l') {
            resolved = get_egl_func(name);
            if (resolved) is_host = true;
        }
        if (!resolved && name[0] == 'v' && name[1] == 'k') {
            resolved = get_vk_func(name);
            if (resolved) is_host = true;
        }

        if (!resolved) {
            void* host_ptr = ::dlsym(RTLD_DEFAULT, name);
            if (host_ptr) {
                resolved = host_ptr;
                is_host = true;
            }
        }

        // C++-mangled reference to a C function (guest .so compiled as C++
        // without extern "C", e.g. `extern int __android_log_print(...)` in
        // tests/test_jni_massive.cpp). Demangle, extract the plain function
        // name, and resolve THAT through the same tables — otherwise every log
        // call silently becomes a no-op dummy and crashes show no log.
        if (!resolved && name[0] == '_' && name[1] == 'Z') {
            int status = 0;
            char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
            if (demangled && status == 0) {
                const std::string full(demangled);
                std::free(demangled);
                const auto paren = full.find('(');
                const auto space = paren == std::string::npos
                                       ? full.rfind(' ')
                                       : full.rfind(' ', paren);
                std::string fnName = paren == std::string::npos
                                         ? full
                                         : full.substr(space == std::string::npos ? 0 : space + 1,
                                                       paren - (space == std::string::npos ? 0 : space + 1));
                const auto colons = fnName.rfind("::");
                if (colons != std::string::npos) fnName = fnName.substr(colons + 2);

                const SymbolEntry* syscalls2 = get_syscall_symbols(&count);
                resolved = resolve_from_list(syscalls2, count, fnName.c_str());
                if (!resolved) {
                    const SymbolEntry* graphics2 = get_graphics_symbols(&count);
                    resolved = resolve_from_list(graphics2, count, fnName.c_str());
                }
                if (!resolved) {
                    const SymbolEntry* input2 = get_input_symbols(&count);
                    resolved = resolve_from_list(input2, count, fnName.c_str());
                }
                if (!resolved) {
                    const SymbolEntry* audio2 = get_audio_symbols(&count);
                    resolved = resolve_from_list(audio2, count, fnName.c_str());
                }
                if (!resolved) {
                    const SymbolEntry* assets2 = get_asset_symbols(&count);
                    resolved = resolve_from_list(assets2, count, fnName.c_str());
                }
                if (resolved) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "mangled %s demangled to %s, bound to shim implementation",
                             name, fnName.c_str());
                    trace_shim(msg);
                }
            }
        }

        // stderr/stdout/stdin are global VARIABLES (FILE*), not functions — bind
        // dummy (code address) causes guest to dereference code-as-FILE* → SEGV.
        // Return the actual host variable address so that guest's fprintf(stderr, ...) runs correctly
        // (and the guest log is dumped into stderr.log which the crash handler reads).
        if (!resolved && std::strcmp(name, "stderr") == 0) {
            resolved = reinterpret_cast<void*>(&::stderr);
            is_host = true;
        } else if (!resolved && std::strcmp(name, "stdout") == 0) {
            resolved = reinterpret_cast<void*>(&::stdout);
            is_host = true;
        } else if (!resolved && std::strcmp(name, "stdin") == 0) {
            resolved = reinterpret_cast<void*>(&::stdin);
            is_host = true;
        }
        // __sF deliberately has no special case. It is bionic's array of the three
        // standard FILE objects, and a guest reaches the streams as &__sF[0..2].
        // Darwin declares the same array with the same meaning, so the dlsym above
        // already binds it to real host streams — which is what the guest's
        // (host-backed) fwrite/fprintf need. Substituting our own array would only
        // help if the guest read FILE fields directly, and would break the common
        // case of passing the pointer straight to host stdio.

        if (!resolved) {
            char msg[256];
            snprintf(msg, sizeof(msg), "missing symbol bound to dummy: %s", name);
            trace_shim(msg);
            // Mirror to standard logs (android_logs.txt + crash buffer) — previously
            // is only in the shim trace so it disappears when looking for relocation
            // unresolved (which is Discord's SIGILL suspect in init).
            //
            // Priority 5 (warn), not 2 (verbose). This line is the record that a guest
            // is about to call something KuDroid does not implement, and it is often
            // the only warning before the failure it causes: six of these named the
            // AChoreographer functions whose absence stopped ULTRAKILL dead, and at
            // verbose they sat among thousands of routine dlsym lines.
            kudroid_android_log_message(5, "BionicShim", msg);
            resolved = reinterpret_cast<void*>(&kudroid_universal_dummy);
        }

        boundSymbols[name] = {resolved, is_host};
    }
    return resolved;
}

void bionic_shim_reset_trace() {
    gShimTrace.clear();
}

const char* bionic_shim_trace() {
    return gShimTrace.c_str();
}

} // namespace kudroid
