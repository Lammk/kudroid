#pragma once

#include <cstddef>

namespace kudroid {

struct SymbolEntry {
    const char* name;
    void* address;
};

// Generic tracking function for buffer layers
void trace_shim(const char* message);

} // namespace kudroid
