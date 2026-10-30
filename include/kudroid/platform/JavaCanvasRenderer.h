#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace kudroid {

class JavaCanvasRenderer {
public:
    static JavaCanvasRenderer& getInstance();

    void init(int width, int height);

    // Canvas state management
    int save();
    void restore();
    void restoreToCount(int count);
    int getSaveCount() const { return saveCount_; }
    void translate(float dx, float dy);
    void scale(float sx, float sy);
    void rotate(float degrees);
    void concat(float a, float b, float c, float d, float tx, float ty);
    void clipRect(float left, float top, float right, float bottom);

    // 2D primitives
    void drawColor(uint32_t argb);
    void drawRect(float left, float top, float right, float bottom, uint32_t argb,
                  int style = 0, float strokeWidth = 1.0f);
    void drawRoundRect(float left, float top, float right, float bottom,
                       float rx, float ry, uint32_t argb,
                       int style = 0, float strokeWidth = 1.0f);
    void drawCircle(float cx, float cy, float radius, uint32_t argb,
                    int style = 0, float strokeWidth = 1.0f);
    void drawOval(float left, float top, float right, float bottom, uint32_t argb,
                  int style = 0, float strokeWidth = 1.0f);
    void drawLine(float x0, float y0, float x1, float y1, uint32_t argb,
                  float strokeWidth = 1.0f);
    void drawPath(const uint8_t* verbs, int verbCount,
                  const float* coords, int coordCount,
                  uint32_t argb, int style = 0, float strokeWidth = 1.0f,
                  bool evenOdd = false);

    // Text rendering
    void drawText(const char* text, float x, float y, uint32_t argb, float textSize,
                  float shadowRadius = 0, float shadowDx = 0, float shadowDy = 0,
                  uint32_t shadowColor = 0);
    static void drawBitmapText(uint32_t* pixels, int width, int height, const char* text,
                               float x, float y, uint32_t color, float size,
                               float shadowRadius, float shadowDx, float shadowDy,
                               uint32_t shadowColor);

    // Bitmap rendering
    void drawBitmap(const uint32_t* pixels, int width, int height, float x, float y,
                    int alpha = 255, uint32_t filterColor = 0, int filterMode = -1);
    void drawBitmapRegion(const uint32_t* pixels, int srcW, int srcH,
                          int srcX, int srcY, int subW, int subH,
                          float dstX, float dstY, float dstW, float dstH,
                          int alpha = 255, uint32_t filterColor = 0, int filterMode = -1);
    void flush();

    int getWidth() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return width_; }
    int getHeight() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return height_; }
    bool hasBoundMetalSurface() const;
    const uint32_t* getBuffer() const { return framebuffer_; }

private:
    JavaCanvasRenderer();
    JavaCanvasRenderer(int width, int height);
    ~JavaCanvasRenderer();

    int width_ = 1080;
    mutable std::recursive_mutex mutex_;
    int height_ = 1920;
    int saveCount_ = 0;
    uint32_t* framebuffer_ = nullptr;
    size_t bufferSize_ = 0;

#if defined(__APPLE__)
    CGColorSpaceRef colorSpace_ = nullptr;
    CGContextRef context_ = nullptr;
#endif
};

} // namespace kudroid
