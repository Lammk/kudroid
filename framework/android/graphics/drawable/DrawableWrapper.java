package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Rect;

/** Forwards everything to one wrapped drawable; subclasses adjust bounds or drawing. */
public class DrawableWrapper extends Drawable implements Drawable.Callback {
    private Drawable mDrawable;

    public DrawableWrapper(Drawable dr) {
        setDrawable(dr);
    }

    public void setDrawable(Drawable dr) {
        if (mDrawable != null) mDrawable.setCallback(null);
        mDrawable = dr;
        if (dr != null) {
            dr.setCallback(this);
            dr.setVisible(isVisible(), true);
            dr.setState(getState());
            dr.setLevel(getLevel());
            onBoundsChange(getBounds());
        }
        invalidateSelf();
    }

    public Drawable getDrawable() {
        return mDrawable;
    }

    @Override
    public void draw(Canvas canvas) {
        if (mDrawable != null) mDrawable.draw(canvas);
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        if (mDrawable != null) mDrawable.setBounds(bounds);
    }

    @Override
    public void setAlpha(int alpha) {
        if (mDrawable != null) mDrawable.setAlpha(alpha);
    }

    @Override
    public int getAlpha() {
        return mDrawable != null ? mDrawable.getAlpha() : 0xFF;
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        if (mDrawable != null) mDrawable.setColorFilter(colorFilter);
    }

    @Override
    public void setTintList(android.content.res.ColorStateList tint) {
        if (mDrawable != null) mDrawable.setTintList(tint);
    }

    @Override
    public void setTintMode(android.graphics.PorterDuff.Mode tintMode) {
        if (mDrawable != null) mDrawable.setTintMode(tintMode);
    }

    @Override
    public int getOpacity() {
        return mDrawable != null ? mDrawable.getOpacity() : android.graphics.PixelFormat.TRANSPARENT;
    }

    @Override
    public boolean isStateful() {
        return mDrawable != null && mDrawable.isStateful();
    }

    @Override
    protected boolean onStateChange(int[] state) {
        return mDrawable != null && mDrawable.setState(state);
    }

    @Override
    protected boolean onLevelChange(int level) {
        return mDrawable != null && mDrawable.setLevel(level);
    }

    @Override
    public boolean setVisible(boolean visible, boolean restart) {
        final boolean changed = super.setVisible(visible, restart);
        if (mDrawable != null) mDrawable.setVisible(visible, restart);
        return changed;
    }

    @Override
    public int getIntrinsicWidth() {
        return mDrawable != null ? mDrawable.getIntrinsicWidth() : -1;
    }

    @Override
    public int getIntrinsicHeight() {
        return mDrawable != null ? mDrawable.getIntrinsicHeight() : -1;
    }

    @Override
    public boolean getPadding(Rect padding) {
        return mDrawable != null && mDrawable.getPadding(padding);
    }

    @Override
    public void invalidateDrawable(Drawable who) {
        invalidateSelf();
    }

    @Override
    public void scheduleDrawable(Drawable who, Runnable what, long when) {
        scheduleSelf(what, when);
    }

    @Override
    public void unscheduleDrawable(Drawable who, Runnable what) {
        unscheduleSelf(what);
    }
}
