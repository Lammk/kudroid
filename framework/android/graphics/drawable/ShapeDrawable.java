package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.content.res.ColorStateList;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.graphics.drawable.shapes.Shape;

/** Draws a Shape with a Paint inside the drawable's bounds. */
public class ShapeDrawable extends Drawable {
    private Shape mShape;
    private final Paint mPaint = new Paint();
    private final Rect mPadding = new Rect();
    private int mIntrinsicWidth = -1;
    private int mIntrinsicHeight = -1;
    private int mAlpha = 255;
    private ColorStateList mTint;
    private PorterDuff.Mode mTintMode = PorterDuff.Mode.SRC_IN;
    private ColorFilter mTintFilter;

    public ShapeDrawable() {}

    public ShapeDrawable(Shape s) {
        mShape = s;
    }

    public Shape getShape() { return mShape; }

    public void setShape(Shape s) {
        mShape = s;
        updateShape();
        invalidateSelf();
    }

    public Paint getPaint() { return mPaint; }

    public void setPadding(int left, int top, int right, int bottom) {
        mPadding.set(left, top, right, bottom);
    }

    public void setPadding(Rect padding) {
        if (padding == null) mPadding.set(0, 0, 0, 0);
        else mPadding.set(padding);
    }

    public void setIntrinsicWidth(int width) { mIntrinsicWidth = width; }
    public void setIntrinsicHeight(int height) { mIntrinsicHeight = height; }

    @Override
    public int getIntrinsicWidth() { return mIntrinsicWidth; }

    @Override
    public int getIntrinsicHeight() { return mIntrinsicHeight; }

    @Override
    public boolean getPadding(Rect padding) {
        padding.set(mPadding);
        return (mPadding.left | mPadding.top | mPadding.right | mPadding.bottom) != 0;
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        updateShape();
    }

    private void updateShape() {
        if (mShape != null) {
            final Rect r = getBounds();
            mShape.resize(r.width(), r.height());
        }
    }

    @Override
    public void draw(Canvas canvas) {
        if (mShape == null || canvas == null) return;
        final Rect r = getBounds();
        final int alpha = (mAlpha * ((mPaint.getColor() >>> 24) & 0xFF)) / 255;
        final int saved = mPaint.getColor();
        final ColorFilter savedFilter = mPaint.getColorFilter();
        if (savedFilter == null) mPaint.setColorFilter(mTintFilter);
        mPaint.setColor((alpha << 24) | (saved & 0x00FFFFFF));
        canvas.save();
        canvas.translate(r.left, r.top);
        mShape.draw(canvas, mPaint);
        canvas.restore();
        mPaint.setColor(saved);
        mPaint.setColorFilter(savedFilter);
    }

    @Override
    public void setAlpha(int alpha) {
        alpha = Math.max(0, Math.min(255, alpha));
        if (mAlpha != alpha) { mAlpha = alpha; invalidateSelf(); }
    }

    @Override
    public int getAlpha() { return mAlpha; }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        if (mPaint.getColorFilter() != colorFilter) {
            mPaint.setColorFilter(colorFilter); invalidateSelf();
        }
    }

    @Override public ColorFilter getColorFilter() { return mPaint.getColorFilter(); }
    @Override public void setTintList(ColorStateList tint) {
        mTint = tint; updateTint(); invalidateSelf();
    }
    @Override public void setTintMode(PorterDuff.Mode mode) {
        mTintMode = mode; updateTint(); invalidateSelf();
    }
    private boolean updateTint() {
        if (mTint == null || mTintMode == null) {
            boolean changed = mTintFilter != null;
            mTintFilter = null;
            return changed;
        }
        int color = mTint.getColorForState(getState(), mTint.getDefaultColor());
        if (mTintFilter instanceof PorterDuffColorFilter
                && ((PorterDuffColorFilter) mTintFilter).getColor() == color
                && ((PorterDuffColorFilter) mTintFilter).getMode() == mTintMode) return false;
        mTintFilter = new PorterDuffColorFilter(color, mTintMode);
        return true;
    }
    @Override public boolean isStateful() { return mTint != null && mTint.isStateful(); }
    @Override protected boolean onStateChange(int[] state) {
        boolean changed = updateTint();
        if (changed) invalidateSelf();
        return changed;
    }

    @Override
    public int getOpacity() {
        return mShape == null ? android.graphics.PixelFormat.TRANSPARENT
                : (mShape.hasAlpha() ? android.graphics.PixelFormat.TRANSLUCENT
                        : android.graphics.PixelFormat.OPAQUE);
    }
}
