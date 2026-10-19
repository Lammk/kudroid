#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace kudroid {
bool rasterize_vector(const std::vector<std::string>& paths, const float* commands,
                      const uint32_t* colors, int width, int height,
                      float viewport_width, float viewport_height, uint32_t* pixels);
}
