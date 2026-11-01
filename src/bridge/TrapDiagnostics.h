#pragma once

#include <cstdint>

namespace kudroid {
// Capture scalar state without locks, allocations or pointer reads.
bool record_trap_diagnostics(int signal, uint64_t pc, uint64_t lr, uint64_t sp,
                             const uint64_t (&registers)[8]);
}
