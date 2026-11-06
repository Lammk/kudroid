#pragma once

namespace kudroid {
void* egl_default_display();
const char* egl_backend_name();
const char* egl_display_extensions(void* display, const char* extensions);
// GL extensions the backend advertises but cannot honour are removed from guest queries.
const char* gl_context_extensions(const char* extensions);
bool gl_extension_hidden(const char* name);
}
