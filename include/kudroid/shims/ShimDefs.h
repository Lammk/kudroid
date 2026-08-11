#pragma once

#include <cstddef>

namespace kudroid {

struct SymbolEntry {
    const char* name;
    void* address;
};

// chức năng theo dõi chung cho các lớp đệm
void trace_shim(const char* message);

} // namespace kudroid
