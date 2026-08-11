package android.graphics;

/**
 * Minimal android.graphics.Canvas implementation.
 *
 * Provides a drawing surface. For KuDroid's minimal framework, this is a stub
 * that records draw operations (to be bridged to Metal/ANGLE later).
 */
public class Canvas {
    private Bitmap mBitmap;
    private int mWidth;
    private int mHeight;

    public Canvas() {
    }

    public Canvas(Bitmap bitmap) {
        mBitmap = bitmap;
        if (bitmap != null) {
            mWidth = bitmap.getWidth();
            mHeight = bitmap.getHeight();
        }
    }

    /**
     * Return the canvas width.
     */
    public int getWidth() {
        return mWidth;
    }

    /**
     * Return the canvas height.
     */
    public int getHeight() {
        return mHeight;
    }

    /**
     * Draw a color.
     */
    public void drawColor(int color) {
    }

    /**
     * Draw a color with a Porter-Duff mode.
     */
    public void drawColor(int color, PorterDuff.Mode mode) {
    }

    /**
     * Draw a bitmap.
     */
    public void drawBitmap(Bitmap bitmap, float left, float top, Paint paint) {
    }

    /**
     * Draw a rect.
     */
    public void drawRect(float left, float top, float right, float bottom, Paint paint) {
    }

    /**
     * Draw a rect.
     */
    public void drawRect(Rect rect, Paint paint) {
    }

    /**
     * Draw a circle.
     */
    public void drawCircle(float cx, float cy, float radius, Paint paint) {
    }

    /**
     * Draw a line.
     */
    public void drawLine(float startX, float startY, float stopX, float stopY, Paint paint) {
    }

    /**
     * Draw text.
     */
    public void drawText(String text, float x, float y, Paint paint) {
    }

    /**
     * Save the canvas state.
     */
    public int save() {
        return 0;
    }

    /**
     * Restore the canvas state.
     */
    public void restore() {
    }

    /**
     * Translate the canvas.
     */
    public void translate(float dx, float dy) {
    }

    /**
     * Scale the canvas.
     */
    public void scale(float sx, float sy) {
    }

    /**
     * Rotate the canvas.
     */
    public void rotate(float degrees) {
    }

    /**
     * Clip to a rect.
     */
    public boolean clipRect(float left, float top, float right, float bottom) {
        return true;
    }

    /**
     * Return the canvas bitmap.
     */
    public Bitmap getBitmap() {
        return mBitmap;
    }
}
