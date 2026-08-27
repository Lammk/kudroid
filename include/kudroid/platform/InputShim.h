#pragma once
#include "ShimDefs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Shared AInputQueue pointer (owned by InputShim) — passed in
// ANativeActivity's onInputQueueCreated callback.
void* kudroid_get_input_queue(void);

#ifdef __cplusplus
}
#endif

namespace kudroid {
const SymbolEntry* get_input_symbols(size_t* count);
}
