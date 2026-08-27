#include "kudroid/platform/JavaCanvasRenderer.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdio>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

extern void* g_metalLayer;
extern int g_metalLayerWidth;
extern int g_metalLayerHeight;

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_blit_canvas_to_layer(void* layer, const void* bits, int width, int height);
#endif

namespace kudroid {

JavaCanvasRenderer& JavaCanvasRenderer::getInstance() {
    static JavaCanvasRenderer instance;
    return instance;
}

JavaCanvasRenderer::JavaCanvasRenderer() {
    init(g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080,
         g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920);
}

JavaCanvasRenderer::~JavaCanvasRenderer() {
    if (framebuffer_) {
        free(framebuffer_);
        framebuffer_ = nullptr;
    }
}

// Convert ARGB Android to RGBA for iOS CoreGraphics & Metal
static inline uint32_t argb_to_rgba(uint32_t argb) {
    uint32_t a = (argb >> 24) & 0xFF;
    uint32_t r = (argb >> 16) & 0xFF;
    uint32_t g = (argb >> 8) & 0xFF;
    uint32_t b = (argb) & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

void JavaCanvasRenderer::init(int width, int height) {
    if (width <= 0) width = 1080;
    if (height <= 0) height = 1920;
    width_ = width;
    height_ = height;
    size_t needed = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(uint32_t);
    if (!framebuffer_ || bufferSize_ < needed) {
        uint32_t* nb = static_cast<uint32_t*>(realloc(framebuffer_, needed));
        if (nb) {
            framebuffer_ = nb;
            bufferSize_ = needed;
        }
    }
    if (framebuffer_) {
        // Android Material Design dark background
        drawColor(0xFF1E1E1E);
    }
}

void JavaCanvasRenderer::drawColor(uint32_t argb) {
    if (!framebuffer_) return;
    uint32_t rgba = argb_to_rgba(argb);
    size_t totalPixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    for (size_t i = 0; i < totalPixels; ++i) {
        framebuffer_[i] = rgba;
    }
}

void JavaCanvasRenderer::drawRect(float left, float top, float right, float bottom, uint32_t argb) {
    if (!framebuffer_) return;
    int l = std::max(0, static_cast<int>(left));
    int t = std::max(0, static_cast<int>(top));
    int r = std::min(width_, static_cast<int>(right));
    int b = std::min(height_, static_cast<int>(bottom));
    if (l >= r || t >= b) return;

    uint32_t rgba = argb_to_rgba(argb);
    for (int y = t; y < b; ++y) {
        uint32_t* row = framebuffer_ + y * width_;
        for (int x = l; x < r; ++x) {
            row[x] = rgba;
        }
    }
}

#if defined(__APPLE__)
void JavaCanvasRenderer::drawText(const char* text, float x, float y, uint32_t argb, float textSize) {
    if (!framebuffer_ || !text || !*text || width_ <= 0 || height_ <= 0) return;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) return;

    // Create a CGBitmapContext that directly wraps our buffer
    CGContextRef context = CGBitmapContextCreate(
        framebuffer_,
        width_,
        height_,
        8,
        width_ * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );
    CGColorSpaceRelease(colorSpace);

    if (!context) return;

    // Set up Android-like coordinate system (Top-Left = 0.0)
    CGContextTranslateCTM(context, 0, height_);
    CGContextScaleCTM(context, 1.0, -1.0);

    // Prepare UTF-8 string to CFString (100% supported Vietnamese with accents and Unicode)
    CFStringRef cfText = CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
    if (!cfText) {
        CGContextRelease(context);
        return;
    }

    // Colors from ARGB
    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8) & 0xFF) / 255.0f;
    float b = (argb & 0xFF) / 255.0f;
    if (a <= 0.0f) a = 1.0f; // Default full alpha if 0

    CGColorRef textColor = CGColorCreateGenericRGB(r, g, b, a);

    // Apple System Font (San Francisco / SF Pro)
    float fontSize = textSize > 0 ? textSize : 16.0f;
    CTFontRef font = CTFontCreateWithName(CFSTR(".SFUI-Regular"), fontSize, nullptr);
    if (!font) {
        font = CTFontCreateWithName(CFSTR("HelveticaNeue"), fontSize, nullptr);
    }

    // Text drawing properties
    CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CFTypeRef values[] = { font, textColor };
    CFDictionaryRef attributes = CFDictionaryCreate(
        kCFAllocatorDefault,
        (const void**)keys,
        (const void**)values,
        2,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    CFAttributedStringRef attrString = CFAttributedStringCreate(kCFAllocatorDefault, cfText, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(attrString);

    if (line) {
        // Coordinates for drawing text
        CGContextSetTextPosition(context, x, y);
        CTLineDraw(line, context);
        CFRelease(line);
    }

    // Clean up CoreGraphics memory
    CFRelease(attrString);
    CFRelease(attributes);
    if (font) CFRelease(font);
    CGColorRelease(textColor);
    CFRelease(cfText);
    CGContextRelease(context);
}
#else
// Simple fallback for Linux test environments
void JavaCanvasRenderer::drawText(const char* text, float x, float y, uint32_t argb, float textSize) {
    (void)text; (void)x; (void)y; (void)argb; (void)textSize;
}
#endif

void JavaCanvasRenderer::drawBitmap(const uint32_t* pixels, int width, int height, float x, float y) {
    if (!framebuffer_ || !pixels || width <= 0 || height <= 0) return;
    int dstX = static_cast<int>(x);
    int dstY = static_cast<int>(y);

    for (int sy = 0; sy < height; ++sy) {
        int py = dstY + sy;
        if (py < 0 || py >= height_) continue;
        uint32_t* dstRow = framebuffer_ + py * width_;
        const uint32_t* srcRow = pixels + sy * width;
        for (int sx = 0; sx < width; ++sx) {
            int px = dstX + sx;
            if (px >= 0 && px < width_) {
                uint32_t argb = srcRow[sx];
                if ((argb >> 24) > 0) {
                    dstRow[px] = argb_to_rgba(argb);
                }
            }
        }
    }
}

void JavaCanvasRenderer::flush() {
    if (!framebuffer_ || width_ <= 0 || height_ <= 0) return;
#if defined(__APPLE__)
    if (g_metalLayer && kudroid_blit_canvas_to_layer) {
        kudroid_blit_canvas_to_layer(g_metalLayer, framebuffer_, width_, height_);
    }
#endif
}

} // namespace kudroid
