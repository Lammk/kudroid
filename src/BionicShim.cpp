#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/shims/SyscallShim.h"
#include "kudroid/shims/GraphicsShim.h"
#include "kudroid/shims/InputShim.h"
#include "kudroid/shims/AudioShim.h"

#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <dlfcn.h>
#include <cstring>

namespace kudroid {
namespace {
thread_local std::string gShimTrace;
} // namespace

void trace_shim(const char* message) {
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
            void* host_ptr = ::dlsym(RTLD_DEFAULT, name);
            if (host_ptr) {
                resolved = host_ptr;
                is_host = true;
            }
        }

        if (!resolved) {
            char msg[256];
            snprintf(msg, sizeof(msg), "missing symbol bound to dummy: %s", name);
            trace_shim(msg);
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
