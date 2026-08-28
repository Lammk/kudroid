package android.graphics;

/**
 * Implement android.graphics.Canvas with direct C++ Metal Native Canvas connection.
 */
public class Canvas {
    private Bitmap mBitmap;
    private int mWidth;
    private int mHeight;
    private float mTranslateX = 0.0f;
    private float mTranslateY = 0.0f;

    // Native JNI methods connect to C++ Metal Pipeline
    private static native void native_drawColor(int color);
    private static native void native_drawRect(float left, float top, float right, float bottom, int color);
    private static native void native_drawText(String text, float x, float y, int color, float textSize);
    private static native void native_drawBitmap(int[] pixels, int width, int height, float x, float y);
    private static native void native_flush();

    // Real surface size, set by kudroid_set_metal_layer from the CAMetalLayer.
    // These used to be hardcoded to 1080x1920, so on any other screen the layout was
    // computed for the wrong extent and everything past the real width was clipped.
    private static native int native_getSurfaceWidth();
    private static native int native_getSurfaceHeight();

    /** Fallback used only if the native surface has not been bound yet. */
    private static final int DEFAULT_WIDTH = 1080;
    private static final int DEFAULT_HEIGHT = 1920;

    public Canvas() {
        int w = DEFAULT_WIDTH;
        int h = DEFAULT_HEIGHT;
        try {
            int nw = native_getSurfaceWidth();
            int nh = native_getSurfaceHeight();
            if (nw > 0 && nh > 0) {
                w = nw;
                h = nh;
            }
        } catch (Throwable ignored) {}
        mWidth = w;
        mHeight = h;
    }

    public Canvas(Bitmap bitmap) {
        this();
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
        // Temporarily rasterize the bounding box
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
