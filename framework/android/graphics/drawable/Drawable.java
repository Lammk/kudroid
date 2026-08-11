package android.graphics.drawable;

/**
 * Minimal android.graphics.drawable.Drawable implementation.
 *
 * A generic drawable. For KuDroid's minimal framework, this is a stub base
 * class.
 */
public abstract class Drawable {
    public Drawable() {
    }

    /**
     * Set the drawable's bounds.
     */
    public void setBounds(int left, int top, int right, int bottom) {
    }

    /**
     * Return the drawable's intrinsic width.
     */
    public int getIntrinsicWidth() {
        return 0;
    }

    /**
     * Return the drawable's intrinsic height.
     */
    public int getIntrinsicHeight() {
        return 0;
    }

    /**
     * Draw the drawable to a canvas.
     */
    public void draw(android.graphics.Canvas canvas) {
    }

    /**
     * Set the drawable's alpha.
     */
    public void setAlpha(int alpha) {
    }

    /**
     * Set the drawable's color filter.
     */
    public void setColorFilter(android.graphics.ColorFilter colorFilter) {
    }

    /**
     * Return the drawable's opacity.
     */
    public int getOpacity() {
        return android.graphics.PixelFormat.OPAQUE;
    }
}
