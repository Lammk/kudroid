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
        if (mBounds.left != left || mBounds.top != top || mBounds.right != right
                || mBounds.bottom != bottom) {
            mBounds.set(left, top, right, bottom);
            onBoundsChange(mBounds);
        }
    }

    protected void onBoundsChange(android.graphics.Rect bounds) {
    }

    public final void copyBounds(android.graphics.Rect bounds) {
        bounds.set(mBounds);
    }

    public android.graphics.Rect getDirtyBounds() {
        return getBounds();
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

    /** The alpha last set; fully opaque unless a subclass tracks it. */
    public int getAlpha() {
        return 0xFF;
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
    public static android.graphics.PorterDuff.Mode parseTintMode(int value, android.graphics.PorterDuff.Mode defaultMode) {
        switch (value) {
            case 3: return android.graphics.PorterDuff.Mode.SRC_OVER;
            case 5: return android.graphics.PorterDuff.Mode.SRC_IN;
            case 9: return android.graphics.PorterDuff.Mode.SRC_ATOP;
            case 14: return android.graphics.PorterDuff.Mode.MULTIPLY;
            case 15: return android.graphics.PorterDuff.Mode.SCREEN;
            case 16: return android.graphics.PorterDuff.Mode.ADD;
            default: return defaultMode;
        }
    }
    public android.graphics.ColorFilter getColorFilter() { return null; }
    public void clearColorFilter() { setColorFilter(null); }

    public void setColorFilter(int color, android.graphics.PorterDuff.Mode mode) {
        setColorFilter(new android.graphics.PorterDuffColorFilter(color, mode));
    }

    public void setTintBlendMode(android.graphics.BlendMode blendMode) {
        setTintMode(android.graphics.BlendMode.blendModeToPorterDuffMode(blendMode));
    }

    private boolean mVisible = true;
    private int mLevel;
    private boolean mAutoMirrored;
    private boolean mFilterBitmap;
    private int mLayoutDirection;
    private final android.graphics.Rect mHotspotBounds = new android.graphics.Rect();

    /** Sets visibility; returns whether it changed. */
    public boolean setVisible(boolean visible, boolean restart) {
        final boolean changed = mVisible != visible;
        if (changed) {
            mVisible = visible;
            invalidateSelf();
        }
        return changed;
    }

    public final boolean isVisible() {
        return mVisible;
    }

    /** Jump any animation to its end state. */
    public void jumpToCurrentState() {
    }

    public void setHotspot(float x, float y) {
    }

    public void setHotspotBounds(int left, int top, int right, int bottom) {
        mHotspotBounds.set(left, top, right, bottom);
    }

    public void getHotspotBounds(android.graphics.Rect outRect) {
        outRect.set(mHotspotBounds);
    }

    /** Fills padding with the drawable's own padding; false when it has none. */
    public boolean getPadding(android.graphics.Rect padding) {
        padding.set(0, 0, 0, 0);
        return false;
    }

    public final boolean setLevel(int level) {
        if (mLevel != level) {
            mLevel = level;
            return onLevelChange(level);
        }
        return false;
    }

    public final int getLevel() {
        return mLevel;
    }

    protected boolean onLevelChange(int level) {
        return false;
    }

    public void setAutoMirrored(boolean mirrored) {
        mAutoMirrored = mirrored;
    }

    public boolean isAutoMirrored() {
        return mAutoMirrored;
    }

    public void setFilterBitmap(boolean filter) {
        mFilterBitmap = filter;
    }

    public boolean isFilterBitmap() {
        return mFilterBitmap;
    }

    public void setDither(boolean dither) {
    }

    public final boolean setLayoutDirection(int layoutDirection) {
        if (mLayoutDirection != layoutDirection) {
            mLayoutDirection = layoutDirection;
            return onLayoutDirectionChanged(layoutDirection);
        }
        return false;
    }

    public int getLayoutDirection() {
        return mLayoutDirection;
    }

    public boolean onLayoutDirectionChanged(int layoutDirection) {
        return false;
    }

    public boolean canApplyTheme() {
        return false;
    }

    public void applyTheme(android.content.res.Resources.Theme t) {
    }

    public boolean isProjected() {
        return false;
    }

    /** Describes the drawable's shape for outline-based clipping and shadows. */
    public void getOutline(android.graphics.Outline outline) {
        outline.setRect(getBounds());
        outline.setAlpha(0);
    }


    public static class ConstantState {
        public ConstantState() {}
        public Drawable newDrawable() { throw new UnsupportedOperationException(); }
        public Drawable newDrawable(android.content.res.Resources resources) { return newDrawable(); }
        public Drawable newDrawable(android.content.res.Resources resources, android.content.res.Resources.Theme theme) { return newDrawable(resources); }
        public int getChangingConfigurations() { return 0; }
    }

}
