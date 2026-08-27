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

    /**
     * set the boundaries of the drawable.
     */
    public void setBounds(int left, int top, int right, int bottom) {
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
