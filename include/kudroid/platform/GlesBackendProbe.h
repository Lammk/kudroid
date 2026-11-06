#pragma once
#include <string>

namespace kudroid {
// Requires a private current context before guest initialization.
std::string probe_gles31();
std::string probe_vulkan_features(void* display);
}
