#pragma once
#include "ShimDefs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Con trỏ AInputQueue dùng chung (do InputShim sở hữu) — truyền vào
// callback onInputQueueCreated của ANativeActivity.
void* kudroid_get_input_queue(void);

#ifdef __cplusplus
}
#endif

namespace kudroid {
const SymbolEntry* get_input_symbols(size_t* count);
}
