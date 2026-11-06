#pragma once

#include <cstdint>

namespace kudroid::kuart {
// Copy resolution history so crash reporting never reads live metadata.
void RecordMethodResolution(uintptr_t thread, const char* text);
void WriteMethodResolutionTrace(int fd, uintptr_t thread);
}
