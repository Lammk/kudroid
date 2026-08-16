#pragma once
#include "ShimDefs.h"

namespace kudroid {
const SymbolEntry* get_graphics_symbols(size_t* count);
void* get_gl_func(const char* name);
void* get_egl_func(const char* name);
void* get_vk_func(const char* name);
}
