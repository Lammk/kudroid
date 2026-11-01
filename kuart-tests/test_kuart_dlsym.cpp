#include "kudroid/BionicShim.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <thread>

extern "C" void* bionic_dlopen(const char*, int);
extern "C" void* bionic_dlsym(void*, const char*);
extern "C" char* bionic_dlerror();
extern "C" void* (*kudroid_guest_symbol_lookup)(const char*);

namespace {
int failures = 0;
int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}
void* Lookup(const char* name) { return kudroid::resolve_bionic_symbol(name); }
}  // namespace

int main() {
    auto old = kudroid_guest_symbol_lookup;
    kudroid_guest_symbol_lookup = &Lookup;
    void* library = bionic_dlopen("libc.so", RTLD_NOW);
    Check(library != nullptr, "Android library shim opens");
    bionic_dlerror();
    Check(bionic_dlsym(library, "__system_property_find") != nullptr,
          "Implemented ABI symbols remain available");
    Check(bionic_dlsym(library, "no_such_abi_symbol_one") == nullptr,
          "dlsym rejects universal dummy resolutions");
    const char* error = bionic_dlerror();
    Check(error && std::strstr(error, "no_such_abi_symbol_one"), "dlerror names the missing symbol");
    Check(bionic_dlerror() == nullptr, "dlerror consumes its error once");
    Check(bionic_dlsym(library, "async_close_monitor_create") == nullptr,
          "Unavailable optional ABI reports absence");
    bionic_dlerror();
    bionic_dlsym(library, "no_such_abi_symbol_main");
    bool worker_ok = false;
    std::thread worker([&] {
        worker_ok = bionic_dlerror() == nullptr;
        bionic_dlsym(library, "no_such_abi_symbol_worker");
        const char* text = bionic_dlerror();
        worker_ok = worker_ok && text && std::strstr(text, "no_such_abi_symbol_worker");
    });
    worker.join();
    error = bionic_dlerror();
    Check(worker_ok && error && std::strstr(error, "no_such_abi_symbol_main"),
          "Dynamic lookup errors are isolated between threads");
    kudroid_guest_symbol_lookup = old;
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
