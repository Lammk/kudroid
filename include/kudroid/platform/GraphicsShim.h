#pragma once

#include "ShimDefs.h"
#include <cstdint>

extern "C" int64_t kudroid_surface_set_frame(int64_t handle, int x, int y, int width, int height);
extern "C" void bionic_ANativeWindow_release(void* window);

namespace kudroid {
const SymbolEntry* get_graphics_symbols(size_t* count);
void* get_gl_func(const char* name);
void* get_egl_func(const char* name);
void* get_vk_func(const char* name);
}

extern "C" bool kudroid_gpu_has_active_surface(void);
extern "C" bool kudroid_gpu_canvas_allowed(void* layer);
extern "C" void kudroid_gpu_attach_vulkan_layer(void* hostLayer);

extern "C" int64_t bionic_kudroid_gl_surface_create(int32_t client_version, int32_t depth_size, int32_t stencil_size);
extern "C" int32_t bionic_kudroid_gl_surface_make_current(int64_t handle);
extern "C" int32_t bionic_kudroid_gl_surface_swap(int64_t handle);
extern "C" void bionic_kudroid_gl_surface_destroy(int64_t handle);
