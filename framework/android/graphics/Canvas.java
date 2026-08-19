package android.graphics;

/**
 * Triển khai android.graphics.Canvas với kết nối trực tiếp C++ Metal Native Canvas.
 */
public class Canvas {
    private Bitmap mBitmap;
    private int mWidth = 1080;
    private int mHeight = 1920;
    private float mTranslateX = 0.0f;
    private float mTranslateY = 0.0f;

    // Native JNI methods kết nối sang C++ Metal Pipeline
    private static native void native_drawColor(int color);
    private static native void native_drawRect(float left, float top, float right, float bottom, int color);
    private static native void native_drawText(String text, float x, float y, int color, float textSize);
    private static native void native_drawBitmap(int[] pixels, int width, int height, float x, float y);
    private static native void native_flush();

    public Canvas() {
    }

    public Canvas(Bitmap bitmap) {
        mBitmap = bitmap;
        if (bitmap != null) {
            mWidth = bitmap.getWidth();
            mHeight = bitmap.getHeight();
        }
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    public void drawColor(int color) {
        try {
            native_drawColor(color);
        } catch (Throwable t) {}
    }

    public void drawColor(int color, PorterDuff.Mode mode) {
        drawColor(color);
    }

    public void drawRect(float left, float top, float right, float bottom, Paint paint) {
        int color = (paint != null) ? paint.getColor() : 0xFFFFFFFF;
        try {
            native_drawRect(left + mTranslateX, top + mTranslateY, right + mTranslateX, bottom + mTranslateY, color);
        } catch (Throwable t) {}
    }

    public void drawRect(Rect rect, Paint paint) {
        if (rect != null) {
            drawRect(rect.left, rect.top, rect.right, rect.bottom, paint);
        }
    }

    public void drawText(String text, float x, float y, Paint paint) {
        if (text == null || text.isEmpty()) return;
        int color = (paint != null) ? paint.getColor() : 0xFFFFFFFF;
        float textSize = (paint != null) ? paint.getTextSize() : 16.0f;
        try {
            native_drawText(text, x + mTranslateX, y + mTranslateY, color, textSize);
        } catch (Throwable t) {}
    }

    public void drawBitmap(Bitmap bitmap, float left, float top, Paint paint) {
        if (bitmap == null) return;
        int[] pixels = bitmap.getPixels();
        if (pixels != null) {
            try {
                native_drawBitmap(pixels, bitmap.getWidth(), bitmap.getHeight(), left + mTranslateX, top + mTranslateY);
            } catch (Throwable t) {}
        }
    }

    public void drawCircle(float cx, float cy, float radius, Paint paint) {
        // Tạm thời rasterize bounding box
        drawRect(cx - radius, cy - radius, cx + radius, cy + radius, paint);
    }

    public void drawLine(float startX, float startY, float stopX, float stopY, Paint paint) {
        drawRect(startX, startY, stopX, stopY + 1.0f, paint);
    }

    public int save() {
        return 0;
    }

    public void restore() {
        mTranslateX = 0.0f;
        mTranslateY = 0.0f;
    }

    public void translate(float dx, float dy) {
        mTranslateX += dx;
        mTranslateY += dy;
    }

    public void scale(float sx, float sy) {
    }

    public void rotate(float degrees) {
    }

    public boolean clipRect(float left, float top, float right, float bottom) {
        return true;
    }

    public void flush() {
        try {
            native_flush();
        } catch (Throwable t) {}
    }

    public Bitmap getBitmap() {
        return mBitmap;
    }
}
