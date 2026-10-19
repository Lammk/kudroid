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

    /** The smallest width the drawable can be drawn at: its intrinsic width, or 0. */
    public int getMinimumWidth() {
        final int width = getIntrinsicWidth();
        return width > 0 ? width : 0;
    }

    /** The smallest height the drawable can be drawn at: its intrinsic height, or 0. */
    public int getMinimumHeight() {
        final int height = getIntrinsicHeight();
        return height > 0 ? height : 0;
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

    private int[] state = new int[0];
    private int changingConfigurations;
    private Callback callback;
    public interface Callback {
        void invalidateDrawable(Drawable who);
        void scheduleDrawable(Drawable who, Runnable what, long when);
        void unscheduleDrawable(Drawable who, Runnable what);
    }
    public void setCallback(Callback value) { callback = value; }
    public Callback getCallback() { return callback; }
    public void invalidateSelf() { if (callback != null) callback.invalidateDrawable(this); }
    public void scheduleSelf(Runnable what, long when) { if (callback != null) callback.scheduleDrawable(this, what, when); }
    public void unscheduleSelf(Runnable what) { if (callback != null) callback.unscheduleDrawable(this, what); }
    public Drawable mutate() { return this; }
    public ConstantState getConstantState() { return null; }
    public void setChangingConfigurations(int value) { changingConfigurations = value; }
    public int getChangingConfigurations() { return changingConfigurations; }
    public int[] getState() { return state; }
    public boolean setState(int[] value) {
        int[] next = value == null ? new int[0] : value;
        if (java.util.Arrays.equals(state, next)) return false;
        state = next.clone(); return onStateChange(state);
    }
    protected boolean onStateChange(int[] value) { return false; }
    public boolean isStateful() { return false; }
    public void setTint(int color) { setTintList(android.content.res.ColorStateList.valueOf(color)); }
    public void setTintList(android.content.res.ColorStateList tint) {}
    public void setTintMode(android.graphics.PorterDuff.Mode mode) {}
    public android.graphics.ColorFilter getColorFilter() { return null; }
    public void clearColorFilter() { setColorFilter(null); }


    public static class ConstantState {
        public ConstantState() {}
        public Drawable newDrawable() { throw new UnsupportedOperationException(); }
        public Drawable newDrawable(android.content.res.Resources resources) { return newDrawable(); }
        public Drawable newDrawable(android.content.res.Resources resources, android.content.res.Resources.Theme theme) { return newDrawable(resources); }
        public int getChangingConfigurations() { return 0; }
    }

}
