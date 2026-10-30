package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;

/** A solid color with alpha modulation and stateful tint. */
public class ColorDrawable extends Drawable {
    private int mBaseColor, mUseColor;
    private final Paint mPaint = new Paint();
    private ColorStateList mTint;
    private PorterDuff.Mode mTintMode = PorterDuff.Mode.SRC_IN;
    private ColorFilter mColorFilter, mTintFilter;

    public ColorDrawable() { this(0); }
    public ColorDrawable(int color) { mBaseColor = mUseColor = color; }
    public int getColor() { return mUseColor; }
    public void setColor(int color) {
        if (mBaseColor != color || mUseColor != color) {
            mBaseColor = mUseColor = color;
            invalidateSelf();
        }
    }
    @Override public void draw(Canvas canvas) {
        if (canvas == null) return;
        mPaint.setColor(mUseColor);
        mPaint.setColorFilter(mColorFilter != null ? mColorFilter : mTintFilter);
        canvas.drawRect(getBounds(), mPaint);
    }
    @Override public void setAlpha(int alpha) {
        alpha = Math.max(0, Math.min(255, alpha));
        int color = (mBaseColor & 0xffffff) | ((mBaseColor >>> 24) * (alpha + (alpha >> 7)) >> 8 << 24);
        if (mUseColor != color) { mUseColor = color; invalidateSelf(); }
    }
    @Override public int getAlpha() { return mUseColor >>> 24; }
    @Override public void setColorFilter(ColorFilter filter) {
        if (mColorFilter != filter) { mColorFilter = filter; invalidateSelf(); }
    }
    @Override public ColorFilter getColorFilter() { return mColorFilter; }
    @Override public void setTintList(ColorStateList tint) {
        mTint = tint; updateTint(); invalidateSelf();
    }
    @Override public void setTintMode(PorterDuff.Mode mode) {
        mTintMode = mode; updateTint(); invalidateSelf();
    }
    private void updateTint() {
        mTintFilter = mTint == null || mTintMode == null ? null : new PorterDuffColorFilter(
                mTint.getColorForState(getState(), mTint.getDefaultColor()), mTintMode);
    }
    @Override public boolean isStateful() { return mTint != null && mTint.isStateful(); }
    @Override protected boolean onStateChange(int[] state) {
        if (mTint == null || mTintMode == null) return false;
        int color = mTint.getColorForState(state, mTint.getDefaultColor());
        if (mTintFilter instanceof PorterDuffColorFilter
                && ((PorterDuffColorFilter) mTintFilter).getColor() == color) return false;
        updateTint(); invalidateSelf(); return true;
    }
    @Override public int getOpacity() {
        if (mColorFilter != null || mTintFilter != null) return android.graphics.PixelFormat.TRANSLUCENT;
        return getAlpha() == 0 ? android.graphics.PixelFormat.TRANSPARENT
                : getAlpha() == 255 ? android.graphics.PixelFormat.OPAQUE : android.graphics.PixelFormat.TRANSLUCENT;
    }
    @Override public int getIntrinsicWidth() { return -1; }
    @Override public int getIntrinsicHeight() { return -1; }
}
