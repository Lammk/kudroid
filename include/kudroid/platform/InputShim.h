#pragma once
#include "ShimDefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shared AInputQueue pointer (owned by InputShim) — passed in
// ANativeActivity's onInputQueueCreated callback.
void* kudroid_get_input_queue(void);
int kudroid_native_input_attached(void);
void kudroid_enqueue_key_event(int action, int keyCode, int64_t downTimeNanos);

#ifdef __cplusplus
}
#endif

namespace kudroid {
const SymbolEntry* get_input_symbols(size_t* count);
}
