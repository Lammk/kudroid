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
    private static native void native_drawBitmapText(int[] pixels, int width, int height,
            String text, float x, float y, int color, float textSize,
            float shadowRadius, float shadowDx, float shadowDy, int shadowColor);
    private static native void native_drawBitmap(int[] pixels, int width, int height, float x, float y);
    private static native void native_flush();

    // Real surface size, set by kudroid_set_metal_layer from the CAMetalLayer.
    // These used to be hardcoded to 1080x1920, so on any other screen the layout was
    // computed for the wrong extent and everything past the real width was clipped.
    private static native int native_getSurfaceWidth();
    private static native int native_getSurfaceHeight();
    private static native boolean native_isSurfaceReady();

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

    /** True only after the host has attached a real framebuffer, not fallback dimensions. */
    public static boolean isSurfaceReady() {
        try {
            return native_isSurfaceReady();
        } catch (Throwable ignored) {
            return false;
        }
    }

    public void drawColor(int color) {
        if (mBitmap != null) {
            mBitmap.eraseColor(color);
            return;
        }
        try {
            native_drawColor(color);
        } catch (Throwable t) {}
    }

    public void drawColor(int color, PorterDuff.Mode mode) {
        drawColor(color);
    }

    public void drawRect(float left, float top, float right, float bottom, Paint paint) {
        int color = (paint != null) ? paint.getColor() : 0xFFFFFFFF;
        if (mBitmap != null) {
            final int l = Math.max(0, (int) (left + mTranslateX));
            final int t = Math.max(0, (int) (top + mTranslateY));
            final int r = Math.min(mWidth, (int) (right + mTranslateX));
            final int b = Math.min(mHeight, (int) (bottom + mTranslateY));
            mBitmap.fillRect(l, t, r, b, color);
            return;
        }
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
        if (mBitmap != null) {
            native_drawBitmapText(mBitmap.getPixels(), mWidth, mHeight, text,
                    x + mTranslateX, y + mTranslateY, color, textSize,
                    paint != null ? paint.getShadowRadius() : 0,
                    paint != null ? paint.getShadowDx() : 0,
                    paint != null ? paint.getShadowDy() : 0,
                    paint != null ? paint.getShadowColor() : 0);
            return;
        }
        try {
            native_drawText(text, x + mTranslateX, y + mTranslateY, color, textSize);
        } catch (Throwable t) {}
    }

    public void drawBitmap(Bitmap bitmap, float left, float top, Paint paint) {
        if (bitmap == null) return;
        if (mBitmap != null) {
            int dx = (int) (left + mTranslateX), dy = (int) (top + mTranslateY);
            int[] source = bitmap.getPixels();
            if (bitmap == mBitmap) source = source.clone();
            int sw = bitmap.getWidth(), sh = bitmap.getHeight();
            int[] target = mBitmap.getPixels();
            int leftClip = Math.max(0, -dx), rightClip = Math.min(sw, mWidth - dx);
            int topClip = Math.max(0, -dy), bottomClip = Math.min(sh, mHeight - dy);
            for (int y = topClip; y < bottomClip; y++) for (int x = leftClip; x < rightClip; x++) {
                int src = source[y * sw + x], sa = src >>> 24;
                if (sa == 0) continue;
                int at = (dy + y) * mWidth + dx + x;
                if (sa == 255) { target[at] = src; continue; }
                int dst = target[at], da = dst >>> 24, inverse = 255 - sa;
                int denominator = sa * 255 + da * inverse;
                int a = (denominator + 127) / 255;
                int r = (((src >> 16) & 255) * sa * 255 + ((dst >> 16) & 255) * da * inverse + denominator / 2) / denominator;
                int g = (((src >> 8) & 255) * sa * 255 + ((dst >> 8) & 255) * da * inverse + denominator / 2) / denominator;
                int b = ((src & 255) * sa * 255 + (dst & 255) * da * inverse + denominator / 2) / denominator;
                target[at] = a << 24 | r << 16 | g << 8 | b;
            }
            return;
        }
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
        if (mBitmap != null) return;
        try {
            native_flush();
        } catch (Throwable t) {}
    }

    public Bitmap getBitmap() {
        return mBitmap;
    }
}
