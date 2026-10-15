package android.graphics.drawable;

/**
 * minimal android.graphics.drawable.drawable implementation.
 *
 * a generic drawable. for kudroid minimal framework, here is one
 * simulation base class.
 */
public abstract class Drawable {
    public Drawable() {
    }

    private final android.graphics.Rect mBounds = new android.graphics.Rect();

    /**
     * set the boundaries of the drawable.
     *
     * Recorded rather than discarded so getBounds() can answer and so subclasses that
     * position sub-drawables (LayerDrawable) have something to work from. A drawable that
     * forgets its bounds draws in the wrong place or not at all.
     */
    public void setBounds(int left, int top, int right, int bottom) {
        mBounds.set(left, top, right, bottom);
    }

    public void setBounds(android.graphics.Rect bounds) {
        if (bounds != null) setBounds(bounds.left, bounds.top, bounds.right, bounds.bottom);
    }

    public final android.graphics.Rect getBounds() {
        return mBounds;
    }

    public final android.graphics.Rect copyBounds() {
        return new android.graphics.Rect(mBounds);
    }

    /**
     * returns the actual width of the drawable.
     */
    public int getIntrinsicWidth() {
        return 0;
    }

    /**
     * returns the actual height of the drawable.
     */
    public int getIntrinsicHeight() {
        return 0;
    }

    /**
     * draw drawable onto a canvas.
     */
    public void draw(android.graphics.Canvas canvas) {
    }

    /**
     * set the drawable's alpha.
     */
    public void setAlpha(int alpha) {
    }

    /**
     * set the drawable's color filter.
     */
    public void setColorFilter(android.graphics.ColorFilter colorFilter) {
    }

    /**
     * returns the opacity of the drawable.
     */
    public int getOpacity() {
        return android.graphics.PixelFormat.OPAQUE;
    }

    public interface Callback {
    }

    public static class ConstantState {
        public ConstantState() {}
    }

}
