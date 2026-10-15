#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>

namespace kudroid {

class JavaCanvasRenderer {
public:
    static JavaCanvasRenderer& getInstance();

    void init(int width, int height);
    void drawColor(uint32_t argb);
    void drawRect(float left, float top, float right, float bottom, uint32_t argb);
    void drawText(const char* text, float x, float y, uint32_t argb, float textSize,
                  float shadowRadius = 0, float shadowDx = 0, float shadowDy = 0,
                  uint32_t shadowColor = 0);
    static void drawBitmapText(uint32_t* pixels, int width, int height, const char* text,
                               float x, float y, uint32_t color, float size,
                               float shadowRadius, float shadowDx, float shadowDy,
                               uint32_t shadowColor);
    void drawBitmap(const uint32_t* pixels, int width, int height, float x, float y);
    void flush();

    int getWidth() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return width_; }
    int getHeight() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return height_; }
    bool hasBoundMetalSurface() const;
    const uint32_t* getBuffer() const { return framebuffer_; }

private:
    JavaCanvasRenderer();
    JavaCanvasRenderer(int width, int height);
    ~JavaCanvasRenderer();

    void drawChar8x16(char c, int x, int y, uint32_t color, int scale);

    int width_ = 1080;
    mutable std::recursive_mutex mutex_;
    int height_ = 1920;
    uint32_t* framebuffer_ = nullptr;
    size_t bufferSize_ = 0;
};

} // namespace kudroid
