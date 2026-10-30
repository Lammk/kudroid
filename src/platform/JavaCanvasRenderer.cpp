#include "kudroid/platform/JavaCanvasRenderer.h"
#include "kudroid/platform/PixelColorFilter.h"
#include "kudroid/platform/GraphicsShim.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>

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
#if defined(__APPLE__)
    if (context_) {
        CGContextRelease(context_);
        context_ = nullptr;
    }
    if (colorSpace_) {
        CGColorSpaceRelease(colorSpace_);
        colorSpace_ = nullptr;
    }
#endif
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

#if defined(__APPLE__)
static uint32_t argb_to_premult_rgba(uint32_t argb) {
    const uint32_t alpha = argb >> 24;
    const uint32_t r = (((argb >> 16) & 255) * alpha + 127) / 255;
    const uint32_t g = (((argb >> 8) & 255) * alpha + 127) / 255;
    const uint32_t b = ((argb & 255) * alpha + 127) / 255;
    return alpha << 24 | b << 16 | g << 8 | r;
}

static inline void setup_paint_context(CGContextRef ctx, uint32_t argb, int style, float strokeWidth) {
    const float a = ((argb >> 24) & 0xFF) / 255.0f;
    const float r = ((argb >> 16) & 0xFF) / 255.0f;
    const float g = ((argb >> 8) & 0xFF) / 255.0f;
    const float b = (argb & 0xFF) / 255.0f;
    CGContextSetRGBFillColor(ctx, r, g, b, a);
    CGContextSetRGBStrokeColor(ctx, r, g, b, a);
    CGContextSetLineWidth(ctx, strokeWidth > 0 ? strokeWidth : 1.0f);
}

static inline void apply_draw_style(CGContextRef ctx, int style) {
    if (style == 1) {
        CGContextStrokePath(ctx);
    } else if (style == 2) {
        CGContextDrawPath(ctx, kCGPathFillStroke);
    } else {
        CGContextFillPath(ctx);
    }
}

static inline CGImageRef create_cgimage_from_pixels(const uint32_t* pixels, int width, int height) {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, pixels, width * height * 4, nullptr);
    CGImageRef image = CGImageCreate(
        width, height, 8, 32, width * 4, cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big,
        provider, nullptr, false, kCGRenderingIntentDefault
    );
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(cs);
    return image;
}
#endif

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
    saveCount_ = 0;

#if defined(__APPLE__)
    if (context_) {
        CGContextRelease(context_);
        context_ = nullptr;
    }
    if (!colorSpace_) {
        colorSpace_ = CGColorSpaceCreateDeviceRGB();
    }
    if (framebuffer_ && width_ > 0 && height_ > 0) {
        context_ = CGBitmapContextCreate(
            framebuffer_,
            width_,
            height_,
            8,
            width_ * 4,
            colorSpace_,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
        );
        if (context_) {
            CGContextTranslateCTM(context_, 0, height_);
            CGContextScaleCTM(context_, 1.0, -1.0);
            CGContextSetInterpolationQuality(context_, kCGInterpolationHigh);
            CGContextSetShouldAntialias(context_, true);
        }
    }
#endif

    if (framebuffer_) {
        drawColor(0xFF1E1E1E);
    }
}

int JavaCanvasRenderer::save() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
#if defined(__APPLE__)
    if (context_) CGContextSaveGState(context_);
#endif
    return ++saveCount_;
}

void JavaCanvasRenderer::restore() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
#if defined(__APPLE__)
    if (context_) CGContextRestoreGState(context_);
#endif
    if (saveCount_ > 0) --saveCount_;
}

void JavaCanvasRenderer::restoreToCount(int count) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    int target = count > 0 ? (count - 1) : 0;
    while (saveCount_ > target) {
#if defined(__APPLE__)
        if (context_) CGContextRestoreGState(context_);
#endif
        --saveCount_;
    }
}

void JavaCanvasRenderer::translate(float dx, float dy) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
#if defined(__APPLE__)
    if (context_) CGContextTranslateCTM(context_, dx, dy);
#endif
}

void JavaCanvasRenderer::scale(float sx, float sy) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
#if defined(__APPLE__)
    if (context_) CGContextScaleCTM(context_, sx, sy);
#endif
}

void JavaCanvasRenderer::rotate(float degrees) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
#if defined(__APPLE__)
    if (context_) CGContextRotateCTM(context_, static_cast<CGFloat>(degrees * (M_PI / 180.0)));
#endif
}

void JavaCanvasRenderer::concat(float a, float b, float c, float d, float tx, float ty) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
#if defined(__APPLE__)
    if (context_) {
        CGAffineTransform m = CGAffineTransformMake(a, b, c, d, tx, ty);
        CGContextConcatCTM(context_, m);
    }
#endif
}

void JavaCanvasRenderer::clipRect(float left, float top, float right, float bottom) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
#if defined(__APPLE__)
    if (context_) {
        CGRect r = CGRectStandardize(CGRectMake(left, top, right - left, bottom - top));
        CGContextClipToRect(context_, r);
    }
#endif
}

void JavaCanvasRenderer::drawColor(uint32_t argb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_) return;
#if defined(__APPLE__)
    if (context_) {
        CGContextSaveGState(context_);
        setup_paint_context(context_, argb, 0, 1.0f);
        CGRect clipBox = CGContextGetClipBoundingBox(context_);
        if (!CGRectIsNull(clipBox)) {
            CGContextFillRect(context_, clipBox);
        }
        CGContextRestoreGState(context_);
        return;
    }
#endif
    uint32_t rgba = argb_to_rgba(argb);
    size_t totalPixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    uint64_t rgba2 = (static_cast<uint64_t>(rgba) << 32) | rgba;
    uint64_t* ptr64 = reinterpret_cast<uint64_t*>(framebuffer_);
    size_t pairs = totalPixels / 2;
    for (size_t i = 0; i < pairs; ++i) {
        ptr64[i] = rgba2;
    }
    if (totalPixels % 2 != 0) {
        framebuffer_[totalPixels - 1] = rgba;
    }
}

void JavaCanvasRenderer::drawRect(float left, float top, float right, float bottom, uint32_t argb,
                                  int style, float strokeWidth) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_) return;
#if defined(__APPLE__)
    if (context_) {
        setup_paint_context(context_, argb, style, strokeWidth);
        CGRect rect = CGRectMake(left, top, right - left, bottom - top);
        if (style == 1) {
            CGContextStrokeRect(context_, rect);
        } else if (style == 2) {
            CGContextFillRect(context_, rect);
            CGContextStrokeRect(context_, rect);
        } else {
            CGContextFillRect(context_, rect);
        }
        return;
    }
#endif
    int l = std::max(0, static_cast<int>(left));
    int t = std::max(0, static_cast<int>(top));
    int r = std::min(width_, static_cast<int>(right));
    int b = std::min(height_, static_cast<int>(bottom));
    if (l >= r || t >= b) return;

    const uint32_t alpha = argb >> 24;
    if (alpha == 0) return;
    uint32_t rgba = argb_to_rgba(argb);
    for (int y = t; y < b; ++y) {
        uint32_t* row = framebuffer_ + y * width_;
        if (alpha == 255) {
            for (int x = l; x < r; ++x) row[x] = rgba;
            continue;
        }
        const uint32_t inverse = 255 - alpha;
        const uint32_t sr = (argb >> 16) & 255, sg = (argb >> 8) & 255, sb = argb & 255;
        for (int x = l; x < r; ++x) {
            const uint32_t dst = row[x];
            const uint32_t nr = (sr * alpha + (dst & 255) * inverse + 127) / 255;
            const uint32_t ng = (sg * alpha + ((dst >> 8) & 255) * inverse + 127) / 255;
            const uint32_t nb = (sb * alpha + ((dst >> 16) & 255) * inverse + 127) / 255;
            const uint32_t na = alpha + ((dst >> 24) * inverse + 127) / 255;
            row[x] = na << 24 | nb << 16 | ng << 8 | nr;
        }
    }
}

void JavaCanvasRenderer::drawRoundRect(float left, float top, float right, float bottom,
                                       float rx, float ry, uint32_t argb,
                                       int style, float strokeWidth) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_) return;
#if defined(__APPLE__)
    if (context_) {
        setup_paint_context(context_, argb, style, strokeWidth);
        CGRect rect = CGRectMake(left, top, right - left, bottom - top);
        CGPathRef roundRect = CGPathCreateWithRoundedRect(rect, rx, ry, nullptr);
        if (roundRect) {
            CGContextAddPath(context_, roundRect);
            apply_draw_style(context_, style);
            CGPathRelease(roundRect);
        }
        return;
    }
#endif
    drawRect(left, top, right, bottom, argb, style, strokeWidth);
}

void JavaCanvasRenderer::drawOval(float left, float top, float right, float bottom,
                                  uint32_t argb, int style, float strokeWidth) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_) return;
#if defined(__APPLE__)
    if (context_) {
        setup_paint_context(context_, argb, style, strokeWidth);
        CGRect rect = CGRectMake(left, top, right - left, bottom - top);
        CGContextAddEllipseInRect(context_, rect);
        apply_draw_style(context_, style);
        return;
    }
#endif
    drawRect(left, top, right, bottom, argb, style, strokeWidth);
}

void JavaCanvasRenderer::drawCircle(float cx, float cy, float radius,
                                    uint32_t argb, int style, float strokeWidth) {
    drawOval(cx - radius, cy - radius, cx + radius, cy + radius, argb, style, strokeWidth);
}

void JavaCanvasRenderer::drawLine(float x0, float y0, float x1, float y1,
                                  uint32_t argb, float strokeWidth) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_) return;
#if defined(__APPLE__)
    if (context_) {
        setup_paint_context(context_, argb, 1, strokeWidth);
        CGContextMoveToPoint(context_, x0, y0);
        CGContextAddLineToPoint(context_, x1, y1);
        CGContextStrokePath(context_);
        return;
    }
#endif
    drawRect(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + 1.0f, std::max(y0, y1) + 1.0f, argb);
}

void JavaCanvasRenderer::drawPath(const uint8_t* verbs, int verbCount,
                                  const float* coords, int coordCount,
                                  uint32_t argb, int style, float strokeWidth, bool evenOdd) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_ || !verbs || verbCount <= 0 || !coords) return;
#if defined(__APPLE__)
    if (context_) {
        CGMutablePathRef path = CGPathCreateMutable();
        int ci = 0;
        for (int i = 0; i < verbCount; ++i) {
            switch (verbs[i]) {
                case 0: // VERB_MOVE
                    if (ci + 2 <= coordCount) {
                        CGPathMoveToPoint(path, nullptr, coords[ci], coords[ci + 1]);
                        ci += 2;
                    }
                    break;
                case 1: // VERB_LINE
                    if (ci + 2 <= coordCount) {
                        CGPathAddLineToPoint(path, nullptr, coords[ci], coords[ci + 1]);
                        ci += 2;
                    }
                    break;
                case 2: // VERB_QUAD
                    if (ci + 4 <= coordCount) {
                        CGPathAddQuadCurveToPoint(path, nullptr, coords[ci], coords[ci + 1],
                                                  coords[ci + 2], coords[ci + 3]);
                        ci += 4;
                    }
                    break;
                case 3: // VERB_CUBIC
                    if (ci + 6 <= coordCount) {
                        CGPathAddCurveToPoint(path, nullptr, coords[ci], coords[ci + 1],
                                              coords[ci + 2], coords[ci + 3],
                                              coords[ci + 4], coords[ci + 5]);
                        ci += 6;
                    }
                    break;
                case 4: // VERB_CLOSE
                    CGPathCloseSubpath(path);
                    break;
                default:
                    break;
            }
        }
        setup_paint_context(context_, argb, style, strokeWidth);
        CGContextAddPath(context_, path);
        if (style == 1) {
            CGContextStrokePath(context_);
        } else if (style == 2) {
            if (evenOdd) CGContextDrawPath(context_, kCGPathEOFillStroke);
            else CGContextDrawPath(context_, kCGPathFillStroke);
        } else {
            if (evenOdd) CGContextEOFillPath(context_);
            else CGContextFillPath(context_);
        }
        CGPathRelease(path);
    }
#endif
}

#if defined(__APPLE__)
void JavaCanvasRenderer::drawText(const char* text, float x, float y, uint32_t argb, float textSize,
                                  float shadowRadius, float shadowDx, float shadowDy,
                                  uint32_t shadowColor) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_ || !text || !*text || width_ <= 0 || height_ <= 0) return;

    if (!context_) return;

    CFStringRef cfText = CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
    if (!cfText) return;

    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8) & 0xFF) / 255.0f;
    float b = (argb & 0xFF) / 255.0f;

    CGColorRef textColor = CGColorCreateGenericRGB(r, g, b, a);

    float fontSize = textSize > 0 ? textSize : 16.0f;
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nullptr);
    if (!font) {
        font = CTFontCreateWithName(CFSTR("HelveticaNeue"), fontSize, nullptr);
    }

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
        CGContextSaveGState(context_);
        if (shadowRadius > 0) {
            CGColorRef shadow = CGColorCreateGenericRGB(
                ((shadowColor >> 16) & 255) / 255.0f, ((shadowColor >> 8) & 255) / 255.0f,
                (shadowColor & 255) / 255.0f, ((shadowColor >> 24) & 255) / 255.0f);
            CGContextSetShadowWithColor(context_, CGSizeMake(shadowDx, shadowDy), shadowRadius, shadow);
            CGColorRelease(shadow);
        }
        CGContextSetTextMatrix(context_, CGAffineTransformMakeScale(1.0, -1.0));
        CGContextSetTextPosition(context_, x, y);
        CTLineDraw(line, context_);
        CGContextRestoreGState(context_);
        CFRelease(line);
    }

    CFRelease(attrString);
    CFRelease(attributes);
    if (font) CFRelease(font);
    CGColorRelease(textColor);
    CFRelease(cfText);
}
#else
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
    if (!pixels || width <= 0 || height <= 0 || !text || !*text) return;
#if defined(__APPLE__)
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        pixels, width, height, 8, width * 4, cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );
    if (ctx) {
        CGContextTranslateCTM(ctx, 0, height);
        CGContextScaleCTM(ctx, 1.0, -1.0);

        CFStringRef cfText = CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
        if (cfText) {
            float a = ((color >> 24) & 0xFF) / 255.0f;
            float r = ((color >> 16) & 0xFF) / 255.0f;
            float g = ((color >> 8) & 0xFF) / 255.0f;
            float b = (color & 0xFF) / 255.0f;
            CGColorRef textColor = CGColorCreateGenericRGB(r, g, b, a);
            float fontSize = size > 0 ? size : 16.0f;
            CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, fontSize, nullptr);
            if (!font) font = CTFontCreateWithName(CFSTR("HelveticaNeue"), fontSize, nullptr);
            CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
            CFTypeRef values[] = { font, textColor };
            CFDictionaryRef attr = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys, (const void**)values, 2,
                                                      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFAttributedStringRef attrStr = CFAttributedStringCreate(kCFAllocatorDefault, cfText, attr);
            CTLineRef line = CTLineCreateWithAttributedString(attrStr);
            if (line) {
                if (shadowRadius > 0) {
                    CGColorRef shadow = CGColorCreateGenericRGB(
                        ((shadowColor >> 16) & 255) / 255.0f, ((shadowColor >> 8) & 255) / 255.0f,
                        (shadowColor & 255) / 255.0f, ((shadowColor >> 24) & 255) / 255.0f);
                    CGContextSetShadowWithColor(ctx, CGSizeMake(shadowDx, shadowDy), shadowRadius, shadow);
                    CGColorRelease(shadow);
                }
                CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0, -1.0));
                CGContextSetTextPosition(ctx, x, y);
                CTLineDraw(line, ctx);
                CFRelease(line);
            }
            CFRelease(attrStr);
            CFRelease(attr);
            if (font) CFRelease(font);
            CGColorRelease(textColor);
            CFRelease(cfText);
        }
        CGContextRelease(ctx);
    }
    CGColorSpaceRelease(cs);
#endif
}

void JavaCanvasRenderer::drawBitmap(const uint32_t* pixels, int width, int height, float x, float y,
                                   int paintAlpha, uint32_t filterColor, int filterMode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_ || !pixels || width <= 0 || height <= 0) return;
#if defined(__APPLE__)
    if (context_) {
        std::vector<uint32_t> rgba(width * height);
        for (int i = 0; i < width * height; ++i)
            rgba[i] = argb_to_premult_rgba(ModulatePixelAlpha(FilterPixelColor(pixels[i], filterColor, filterMode), paintAlpha));
        CGImageRef img = create_cgimage_from_pixels(rgba.data(), width, height);
        if (img) {
            CGContextSaveGState(context_);
            CGContextTranslateCTM(context_, x, y + height);
            CGContextScaleCTM(context_, 1.0, -1.0);
            CGContextDrawImage(context_, CGRectMake(0, 0, width, height), img);
            CGContextRestoreGState(context_);
            CGImageRelease(img);
            return;
        }
    }
#endif
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
                uint32_t argb = ModulatePixelAlpha(FilterPixelColor(srcRow[sx], filterColor, filterMode), paintAlpha);
                const uint32_t alpha = argb >> 24;
                if (alpha == 0) continue;
                if (alpha == 255) { dstRow[px] = argb_to_rgba(argb); continue; }
                const uint32_t dst = dstRow[px], inverse = 255 - alpha;
                const uint32_t r = (((argb >> 16) & 255) * alpha + (dst & 255) * inverse + 127) / 255;
                const uint32_t g = (((argb >> 8) & 255) * alpha + ((dst >> 8) & 255) * inverse + 127) / 255;
                const uint32_t b = ((argb & 255) * alpha + ((dst >> 16) & 255) * inverse + 127) / 255;
                const uint32_t a = alpha + ((dst >> 24) * inverse + 127) / 255;
                dstRow[px] = a << 24 | b << 16 | g << 8 | r;
            }
        }
    }
}

void JavaCanvasRenderer::drawBitmapRegion(const uint32_t* pixels, int srcW, int srcH,
                                          int srcX, int srcY, int subW, int subH,
                                         float dstX, float dstY, float dstW, float dstH,
                                         int paintAlpha, uint32_t filterColor, int filterMode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!framebuffer_ || !pixels || srcW <= 0 || srcH <= 0 || subW <= 0 || subH <= 0 || dstW <= 0 || dstH <= 0) return;
#if defined(__APPLE__)
    if (context_) {
        std::vector<uint32_t> rgba(subW * subH);
        for (int r = 0; r < subH; ++r) {
            const uint32_t* srcRow = pixels + (srcY + r) * srcW + srcX;
            uint32_t* dstRow = rgba.data() + r * subW;
            for (int c = 0; c < subW; ++c) {
                dstRow[c] = argb_to_premult_rgba(ModulatePixelAlpha(FilterPixelColor(srcRow[c], filterColor, filterMode), paintAlpha));
            }
        }
        CGImageRef img = create_cgimage_from_pixels(rgba.data(), subW, subH);
        if (img) {
            CGContextSaveGState(context_);
            CGContextTranslateCTM(context_, dstX, dstY + dstH);
            CGContextScaleCTM(context_, 1.0, -1.0);
            CGContextDrawImage(context_, CGRectMake(0, 0, dstW, dstH), img);
            CGContextRestoreGState(context_);
            CGImageRelease(img);
            return;
        }
    }
#endif
    int dx0 = std::max(0, static_cast<int>(dstX));
    int dy0 = std::max(0, static_cast<int>(dstY));
    int dx1 = std::min(width_, static_cast<int>(dstX + dstW));
    int dy1 = std::min(height_, static_cast<int>(dstY + dstH));
    if (dx0 >= dx1 || dy0 >= dy1) return;

    for (int dy = dy0; dy < dy1; ++dy) {
        int sy = srcY + static_cast<int>((dy - dstY) * subH / dstH);
        if (sy < 0 || sy >= srcH) continue;
        uint32_t* dstRow = framebuffer_ + dy * width_;
        const uint32_t* srcRow = pixels + sy * srcW;
        for (int dx = dx0; dx < dx1; ++dx) {
            int sx = srcX + static_cast<int>((dx - dstX) * subW / dstW);
            if (sx < 0 || sx >= srcW) continue;
            uint32_t argb = ModulatePixelAlpha(FilterPixelColor(srcRow[sx], filterColor, filterMode), paintAlpha);
            const uint32_t alpha = argb >> 24;
            if (alpha == 0) continue;
            if (alpha == 255) { dstRow[dx] = argb_to_rgba(argb); continue; }
            const uint32_t dst = dstRow[dx], inverse = 255 - alpha;
            const uint32_t r = (((argb >> 16) & 255) * alpha + (dst & 255) * inverse + 127) / 255;
            const uint32_t g = (((argb >> 8) & 255) * alpha + ((dst >> 8) & 255) * inverse + 127) / 255;
            const uint32_t b = ((argb & 255) * alpha + ((dst >> 16) & 255) * inverse + 127) / 255;
            const uint32_t a = alpha + ((dst >> 24) * inverse + 127) / 255;
            dstRow[dx] = a << 24 | b << 16 | g << 8 | r;
        }
    }
}

void JavaCanvasRenderer::flush() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    restoreToCount(0);
    if (!framebuffer_ || width_ <= 0 || height_ <= 0) return;
    if (!kudroid_gpu_canvas_allowed(g_metalLayer)) return;
#if defined(__APPLE__)
    if (g_metalLayer && kudroid_blit_canvas_to_layer) {
        kudroid_blit_canvas_to_layer(g_metalLayer, framebuffer_, width_, height_);
    }
#endif
}

} // namespace kudroid
