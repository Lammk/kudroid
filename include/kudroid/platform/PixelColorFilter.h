#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kudroid {

// PorterDuff filters combine a constant source with the pixel destination.
inline uint32_t FilterPixelColor(uint32_t pixel, uint32_t color, int mode) {
    if (mode < 0 || mode == 2) return pixel;
    if (mode == 0) return 0;
    if (mode == 1) return color;
    if (mode == 5) {
        const uint32_t alpha = ((pixel >> 24) * (color >> 24) + 127) / 255;
        return alpha == 0 ? 0 : (alpha << 24) | (color & 0xffffff);
    }
    const double sa = (color >> 24) / 255.0, da = (pixel >> 24) / 255.0;
    double sf = 0, df = 0, alpha = 0;
    switch (mode) {
        case 3: sf = 1; df = 1 - sa; break;
        case 4: sf = 1 - da; df = 1; break;
        case 5: sf = da; break;
        case 6: df = sa; break;
        case 7: sf = 1 - da; break;
        case 8: df = 1 - sa; break;
        case 9: sf = da; df = 1 - sa; break;
        case 10: sf = 1 - da; df = sa; break;
        case 11: sf = 1 - da; df = 1 - sa; break;
        case 14: alpha = sa * da; break;
        case 16: alpha = std::min(1.0, sa + da); break;
        default: alpha = sa + da - sa * da; break;
    }
    if (mode <= 11) alpha = sa * sf + da * df;
    if (alpha <= 0) return 0;
    uint32_t result = static_cast<uint32_t>(std::lround(alpha * 255)) << 24;
    for (int shift : {16, 8, 0}) {
        const double s = ((color >> shift) & 255) / 255.0;
        const double d = ((pixel >> shift) & 255) / 255.0;
        double out;
        if (mode <= 11) out = s * sa * sf + d * da * df;
        else if (mode == 14) out = s * d * sa * da;
        else if (mode == 16) out = std::min(1.0, s * sa + d * da);
        else {
            double blend = 0;
            switch (mode) {
                case 12: blend = std::min(s, d); break;
                case 13: blend = std::max(s, d); break;
                case 15: blend = s + d - s * d; break;
                case 17: blend = d <= 0.5 ? 2 * s * d : 1 - 2 * (1 - s) * (1 - d); break;
                default: return pixel;
            }
            out = s * sa * (1 - da) + d * da * (1 - sa) + blend * sa * da;
        }
        result |= static_cast<uint32_t>(std::lround(std::clamp(out / alpha, 0.0, 1.0) * 255)) << shift;
    }
    return result;
}

inline uint32_t ModulatePixelAlpha(uint32_t pixel, int alpha) {
    return (pixel & 0xffffff) | (((pixel >> 24) * std::clamp(alpha, 0, 255) + 127) / 255 << 24);
}

}  // namespace kudroid
