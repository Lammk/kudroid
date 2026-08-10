#pragma once

#include <cstddef>

namespace kudroid {

struct SymbolEntry {
    const char* name;
    void* address;
};

// Common trace function for shims
void trace_shim(const char* message);

} // namespace kudroid
