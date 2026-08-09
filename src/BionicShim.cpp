#include "kudroid/BionicShim.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

namespace kudroid {
namespace {

extern "C" int bionic_dummy() {
    return 0;
}

extern "C" int bionic_android_log_print(int priority, const char* tag,
                                          const char* format, ...) {
    (void)priority;
    std::fprintf(stdout, "[AndroidLog][%s]: ", tag ? tag : " unknown");

    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format ? format : "", args);
    va_end(args);

    std::fputc('\n', stdout);
    return 0;
}

struct SymbolEntry {
    const char* name;
    void* address;
};

const SymbolEntry kSymbols[] = {
    {"malloc", reinterpret_cast<void*>(&std::malloc)},
    {"calloc", reinterpret_cast<void*>(&std::calloc)},
    {"realloc", reinterpret_cast<void*>(&std::realloc)},
    {"free", reinterpret_cast<void*>(&std::free)},
    {"mmap", reinterpret_cast<void*>(&::mmap)},
    {"munmap", reinterpret_cast<void*>(&::munmap)},
    {"pthread_create", reinterpret_cast<void*>(&::pthread_create)},
    {"pthread_join", reinterpret_cast<void*>(&::pthread_join)},
    {"pthread_mutex_init", reinterpret_cast<void*>(&::pthread_mutex_init)},
    {"pthread_mutex_lock", reinterpret_cast<void*>(&::pthread_mutex_lock)},
    {"pthread_mutex_unlock", reinterpret_cast<void*>(&::pthread_mutex_unlock)},
    {"__android_log_print", reinterpret_cast<void*>(&bionic_android_log_print)},
};

} // namespace

void* resolve_bionic_symbol(const char* name) {
    if (name) {
        for (const auto& symbol : kSymbols) {
            if (std::strcmp(name, symbol.name) == 0) {
                return symbol.address;
            }
        }

        std::fprintf(stderr, "Missing Bionic symbol: %s\n", name);
    } else {
        std::fprintf(stderr, "Missing Bionic symbol: <null>\n");
    }

    return reinterpret_cast<void*>(&bionic_dummy);
}

} // namespace kudroid
