#include "kudroid/abi/SyscallShim.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
int failures = 0;
int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}
void* Symbol(const char* name) {
    size_t count = 0;
    auto symbols = kudroid::get_syscall_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(symbols[i].name, name) == 0) return symbols[i].address;
    }
    return nullptr;
}
using Find = const void* (*)(const char*);
using Read = int (*)(const void*, char*, char*);
using ReadCallback = void (*)(const void*, void (*)(void*, const char*, const char*, unsigned), void*);
using ForEach = int (*)(void (*)(const void*, void*), void*);
struct State {
    Find find;
    Read read;
    ReadCallback readCallback;
    std::set<std::string> names;
};
void CheckValue(void* cookie, const char* name, const char* value, unsigned) {
    auto* state = static_cast<State*>(cookie);
    char actual[92]{};
    state->read(state->find(name), nullptr, actual);
    Check(std::strcmp(actual, value) == 0, "Read callback can reenter property lookup and read");
}
void Visit(const void* property, void* cookie) {
    auto* state = static_cast<State*>(cookie);
    char name[256]{}, value[92]{};
    int length = state->read(property, name, value);
    Check(length == static_cast<int>(std::strlen(value)), "Enumeration exposes readable property records");
    Check(state->find(name) == property, "Enumeration and find return the same opaque handle");
    Check(state->names.insert(name).second, "Each property is enumerated exactly once");
    state->readCallback(property, &CheckValue, cookie);
}
}  // namespace

int main() {
    auto each = reinterpret_cast<ForEach>(Symbol("__system_property_foreach"));
    State state{reinterpret_cast<Find>(Symbol("__system_property_find")),
                reinterpret_cast<Read>(Symbol("__system_property_read")),
                reinterpret_cast<ReadCallback>(Symbol("__system_property_read_callback")), {}};
    Check(each && state.find && state.read && state.readCallback, "Property ABI symbols are exported");
    if (each && state.find && state.read && state.readCallback) {
        Check(each(&Visit, &state) == 0, "Enumeration returns success");
        Check(state.names.count("ro.build.version.sdk") == 1, "Enumeration includes SDK identity");
        Check(state.names.count("ro.product.cpu.abi") == 1, "Enumeration includes CPU ABI");
        Check(state.find("not.a.known.property") == nullptr, "Unknown properties remain absent");
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
