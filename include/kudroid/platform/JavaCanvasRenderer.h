#pragma once

#include <cstdint>
#include <cstddef>

namespace kudroid {

class JavaCanvasRenderer {
public:
    static JavaCanvasRenderer& getInstance();

    void init(int width, int height);
    void drawColor(uint32_t argb);
    void drawRect(float left, float top, float right, float bottom, uint32_t argb);
    void drawText(const char* text, float x, float y, uint32_t argb, float textSize);
    void drawBitmap(const uint32_t* pixels, int width, int height, float x, float y);
    void flush();

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    const uint32_t* getBuffer() const { return framebuffer_; }

private:
    JavaCanvasRenderer();
    ~JavaCanvasRenderer();

    void drawChar8x16(char c, int x, int y, uint32_t color, int scale);

    int width_ = 1080;
    int height_ = 1920;
    uint32_t* framebuffer_ = nullptr;
    size_t bufferSize_ = 0;
};

} // namespace kudroid
