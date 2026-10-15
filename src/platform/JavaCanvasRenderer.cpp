#include "kudroid/platform/JavaCanvasRenderer.h"
#include "kudroid/platform/GraphicsShim.h"
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

bool JavaCanvasRenderer::hasBoundMetalSurface() const {
    return g_metalLayer != nullptr && g_metalLayerWidth > 0 && g_metalLayerHeight > 0;
}

JavaCanvasRenderer::JavaCanvasRenderer() {
    init(g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080,
         g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920);
}

JavaCanvasRenderer::JavaCanvasRenderer(int width, int height) {
    init(width, height);
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (width <= 0) width = 1080;
    if (height <= 0) height = 1920;
    size_t needed = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint32_t);
    if (!framebuffer_ || bufferSize_ < needed) {
        uint32_t* nb = static_cast<uint32_t*>(realloc(framebuffer_, needed));
        if (nb) {
            framebuffer_ = nb;
            bufferSize_ = needed;
        } else return;
    }
    width_ = width;
    height_ = height;
    if (framebuffer_) {
        // Android Material Design dark background
        drawColor(0xFF1E1E1E);
    }
}

void JavaCanvasRenderer::drawColor(uint32_t argb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_) return;
    uint32_t rgba = argb_to_rgba(argb);
    size_t totalPixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    for (size_t i = 0; i < totalPixels; ++i) {
        framebuffer_[i] = rgba;
    }
}

void JavaCanvasRenderer::drawRect(float left, float top, float right, float bottom, uint32_t argb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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
void JavaCanvasRenderer::drawText(const char* text, float x, float y, uint32_t argb, float textSize,
                                  float shadowRadius, float shadowDx, float shadowDy,
                                  uint32_t shadowColor) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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

    CGColorRef textColor = CGColorCreateGenericRGB(r, g, b, a);

    // Apple System Font (San Francisco / SF Pro)
    float fontSize = textSize > 0 ? textSize : 16.0f;
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nullptr);
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
        if (shadowRadius > 0) {
            CGColorRef shadow = CGColorCreateGenericRGB(
                ((shadowColor >> 16) & 255) / 255.0, ((shadowColor >> 8) & 255) / 255.0,
                (shadowColor & 255) / 255.0, (shadowColor >> 24) / 255.0);
            CGContextSetShadowWithColor(context, CGSizeMake(shadowDx, shadowDy), shadowRadius, shadow);
            CGColorRelease(shadow);
        }
        // CoreText glyphs draw upside down in a vertically flipped CGContext unless
        // TextMatrix is also flipped with CGAffineTransformMakeScale(1.0, -1.0).
        CGContextSetTextMatrix(context, CGAffineTransformMakeScale(1.0, -1.0));
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
void JavaCanvasRenderer::drawText(const char* text, float x, float y, uint32_t argb, float textSize,
                                  float shadowRadius, float shadowDx, float shadowDy,
                                  uint32_t shadowColor) {
    (void)text; (void)x; (void)y; (void)argb; (void)textSize;
    (void)shadowRadius; (void)shadowDx; (void)shadowDy; (void)shadowColor;
}
#endif

void JavaCanvasRenderer::drawBitmapText(uint32_t* pixels, int width, int height, const char* text,
                                        float x, float y, uint32_t color, float size,
                                        float shadowRadius, float shadowDx, float shadowDy,
                                        uint32_t shadowColor) {
    if (!pixels || width <= 0 || height <= 0) return;
    JavaCanvasRenderer target(width, height);
    if (!target.framebuffer_ || target.bufferSize_ < static_cast<size_t>(width) * height * 4) return;
    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t p = pixels[i], a = p >> 24;
        target.framebuffer_[i] = (a << 24) | (((p & 255) * a / 255) << 16) |
                                ((((p >> 8) & 255) * a / 255) << 8) |
                                (((p >> 16) & 255) * a / 255);
    }
    target.drawText(text, x, y, color, size, shadowRadius, shadowDx, shadowDy, shadowColor);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t p = target.framebuffer_[i], a = p >> 24;
        pixels[i] = a ? (a << 24) | (std::min(255u, (p & 255) * 255 / a) << 16) |
                       (std::min(255u, ((p >> 8) & 255) * 255 / a) << 8) |
                       std::min(255u, ((p >> 16) & 255) * 255 / a) : 0;
    }
}

void JavaCanvasRenderer::drawBitmap(const uint32_t* pixels, int width, int height, float x, float y) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_ || width_ <= 0 || height_ <= 0) return;
    if (!kudroid_gpu_canvas_allowed(g_metalLayer)) return;
#if defined(__APPLE__)
    if (g_metalLayer && kudroid_blit_canvas_to_layer) {
        kudroid_blit_canvas_to_layer(g_metalLayer, framebuffer_, width_, height_);
    }
#endif
}

} // namespace kudroid
