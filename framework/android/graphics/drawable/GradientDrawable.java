package android.graphics.drawable;

import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.graphics.RectF;

/**
 * A shape filled with a solid color or gradient. Gradients are drawn as their first color
 * and rounded corners are honoured only by the canvas's own rounded-rect support.
 */
public class GradientDrawable extends Drawable {
    public static final int RECTANGLE = 0;
    public static final int OVAL = 1;
    public static final int LINE = 2;
    public static final int RING = 3;
    public static final int LINEAR_GRADIENT = 0;
    public static final int RADIAL_GRADIENT = 1;
    public static final int SWEEP_GRADIENT = 2;

    public enum Orientation {
        TOP_BOTTOM, TR_BL, RIGHT_LEFT, BR_TL, BOTTOM_TOP, BL_TR, LEFT_RIGHT, TL_BR
    }

    private int mShape = RECTANGLE;
    private int mGradientType = LINEAR_GRADIENT;
    private Orientation mOrientation = Orientation.TOP_BOTTOM;
    private int[] mGradientColors;
    private float[] mPositions;
    private ColorStateList mFillColors;
    private int mFillColor = 0;
    private boolean mHasFill;
    private int mStrokeWidth = -1;
    private ColorStateList mStrokeColors;
    private int mStrokeColor;
    private float mRadius;
    private float[] mRadiusArray;
    private int mWidth = -1;
    private int mHeight = -1;
    private float mGradientRadius;
    private float mCenterX = 0.5f;
    private float mCenterY = 0.5f;
    private boolean mUseLevel;
    private Rect mPadding;
    private int mAlpha = 255;
    private ColorStateList mTint;
    private PorterDuff.Mode mTintMode = PorterDuff.Mode.SRC_IN;
    private ColorFilter mColorFilter, mTintFilter;
    private final Paint mPaint = new Paint();
    private final RectF mRect = new RectF();

    public GradientDrawable() {}

    public GradientDrawable(Orientation orientation, int[] colors) {
        mOrientation = orientation;
        setColors(colors);
    }

    public int getShape() { return mShape; }

    public void setShape(int shape) {
        mShape = shape;
        invalidateSelf();
    }

    public void setGradientType(int gradient) {
        mGradientType = gradient;
        invalidateSelf();
    }

    public int getGradientType() { return mGradientType; }

    public void setGradientCenter(float x, float y) {
        mCenterX = x;
        mCenterY = y;
    }

    public float getGradientCenterX() { return mCenterX; }
    public float getGradientCenterY() { return mCenterY; }

    public void setGradientRadius(float gradientRadius) { mGradientRadius = gradientRadius; }
    public float getGradientRadius() { return mGradientRadius; }

    public void setUseLevel(boolean useLevel) { mUseLevel = useLevel; }
    public boolean getUseLevel() { return mUseLevel; }

    public Orientation getOrientation() { return mOrientation; }

    public void setOrientation(Orientation orientation) {
        mOrientation = orientation;
        invalidateSelf();
    }

    public void setCornerRadii(float[] radii) {
        mRadiusArray = radii;
        if (radii == null) mRadius = 0;
        invalidateSelf();
    }

    public float[] getCornerRadii() {
        return mRadiusArray != null ? mRadiusArray.clone() : null;
    }

    public void setCornerRadius(float radius) {
        mRadius = radius < 0 ? 0 : radius;
        mRadiusArray = null;
        invalidateSelf();
    }

    public float getCornerRadius() { return mRadius; }

    public void setStroke(int width, int color) {
        setStroke(width, ColorStateList.valueOf(color));
    }

    public void setStroke(int width, ColorStateList colorStateList) {
        mStrokeWidth = width;
        mStrokeColors = colorStateList;
        mStrokeColor = colorStateList != null
                ? colorStateList.getColorForState(getState(), colorStateList.getDefaultColor()) : 0;
        invalidateSelf();
    }

    public void setStroke(int width, int color, float dashWidth, float dashGap) {
        setStroke(width, color);
    }

    public void setStroke(int width, ColorStateList colorStateList, float dashWidth, float dashGap) {
        setStroke(width, colorStateList);
    }

    public void setSize(int width, int height) {
        mWidth = width;
        mHeight = height;
        invalidateSelf();
    }

    public void setPadding(int left, int top, int right, int bottom) {
        if (mPadding == null) mPadding = new Rect();
        mPadding.set(left, top, right, bottom);
    }

    public void setColors(int[] colors) {
        setColors(colors, null);
    }

    public void setColors(int[] colors, float[] offsets) {
        mGradientColors = colors;
        mPositions = offsets;
        invalidateSelf();
    }

    public int[] getColors() {
        return mGradientColors != null ? mGradientColors.clone() : null;
    }

    public void setColor(int argb) {
        setColor(ColorStateList.valueOf(argb));
    }

    public void setColor(ColorStateList colors) {
        mFillColors = colors;
        mHasFill = colors != null;
        mFillColor = colors != null ? colors.getColorForState(getState(), colors.getDefaultColor()) : 0;
        invalidateSelf();
    }

    public ColorStateList getColor() { return mFillColors; }

    private int effectiveFill() {
        if (mHasFill) return mFillColor;
        if (mGradientColors != null && mGradientColors.length > 0) return mGradientColors[0];
        return 0;
    }

    @Override
    public void draw(Canvas canvas) {
        if (canvas == null) return;
        final Rect bounds = getBounds();
        if (bounds.isEmpty()) return;
        mPaint.setColorFilter(mColorFilter != null ? mColorFilter : mTintFilter);
        mRect.set(bounds.left, bounds.top, bounds.right, bounds.bottom);
        final int fill = effectiveFill();
        final int fillAlpha = (mAlpha * ((fill >>> 24) & 0xFF)) / 255;
        if (fillAlpha != 0 || mColorFilter != null || mTintFilter != null) {
            mPaint.setColor((fillAlpha << 24) | (fill & 0x00FFFFFF));
            drawShape(canvas, mRect);
        }
        if (mStrokeWidth > 0 && ((mStrokeColor >>> 24) & 0xFF) != 0) {
            final int strokeAlpha = (mAlpha * ((mStrokeColor >>> 24) & 0xFF)) / 255;
            mPaint.setColor((strokeAlpha << 24) | (mStrokeColor & 0x00FFFFFF));
            final float w = mStrokeWidth;
            canvas.drawRect(mRect.left, mRect.top, mRect.right, mRect.top + w, mPaint);
            canvas.drawRect(mRect.left, mRect.bottom - w, mRect.right, mRect.bottom, mPaint);
            canvas.drawRect(mRect.left, mRect.top + w, mRect.left + w, mRect.bottom - w, mPaint);
            canvas.drawRect(mRect.right - w, mRect.top + w, mRect.right, mRect.bottom - w, mPaint);
        }
    }

    private void drawShape(Canvas canvas, RectF r) {
        switch (mShape) {
            case OVAL:
                canvas.drawOval(r, mPaint);
                break;
            case LINE:
                canvas.drawRect(r.left, r.centerY(), r.right,
                        r.centerY() + Math.max(1, mStrokeWidth), mPaint);
                break;
            default:
                final float radius = mRadiusArray != null ? mRadiusArray[0] : mRadius;
                if (radius > 0) canvas.drawRoundRect(r, radius, radius, mPaint);
                else canvas.drawRect(r, mPaint);
        }
    }

    @Override
    public boolean isStateful() {
        return (mFillColors != null && mFillColors.isStateful())
                || (mStrokeColors != null && mStrokeColors.isStateful())
                || (mTint != null && mTint.isStateful());
    }

    @Override
    protected boolean onStateChange(int[] stateSet) {
        boolean changed = false;
        if (mFillColors != null) {
            final int color = mFillColors.getColorForState(stateSet, mFillColors.getDefaultColor());
            if (color != mFillColor) {
                mFillColor = color;
                changed = true;
            }
        }
        if (mStrokeColors != null) {
            final int color = mStrokeColors.getColorForState(stateSet, mStrokeColors.getDefaultColor());
            if (color != mStrokeColor) {
                mStrokeColor = color;
                changed = true;
            }
        }
        changed |= updateTint();
        if (changed) invalidateSelf();
        return changed;
    }

    @Override
    public void setAlpha(int alpha) {
        mAlpha = Math.max(0, Math.min(255, alpha));
        invalidateSelf();
    }

    @Override
    public int getAlpha() { return mAlpha; }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        if (mColorFilter != colorFilter) { mColorFilter = colorFilter; invalidateSelf(); }
    }

    @Override public ColorFilter getColorFilter() { return mColorFilter; }
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

    @Override
    public int getOpacity() {
        return android.graphics.PixelFormat.TRANSLUCENT;
    }

    @Override
    public int getIntrinsicWidth() { return mWidth; }

    @Override
    public int getIntrinsicHeight() { return mHeight; }

    @Override
    public boolean getPadding(Rect padding) {
        if (mPadding == null) return super.getPadding(padding);
        padding.set(mPadding);
        return true;
    }
}
