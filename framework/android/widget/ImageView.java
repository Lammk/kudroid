package android.widget;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.view.View;

public class ImageView extends View {
    public enum ScaleType {
        MATRIX,
        FIT_XY,
        FIT_START,
        FIT_CENTER,
        FIT_END,
        CENTER,
        CENTER_CROP,
        CENTER_INSIDE
    }

    private Drawable mDrawable;
    private ScaleType mScaleType = ScaleType.FIT_CENTER;

    public ImageView() { super(null); }
    public ImageView(Context context) { this(context, null); }
    public ImageView(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public ImageView(Context context, android.util.AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public ImageView(Context context, android.util.AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (context == null) return;
        android.content.res.TypedArray a = context.obtainStyledAttributes(
                attrs, com.android.internal.R.styleable.ImageView, defStyleAttr, defStyleRes);
        try {
            setImageDrawable(a.getDrawable(com.android.internal.R.styleable.ImageView_src));
            mBaseline = a.getDimensionPixelSize(com.android.internal.R.styleable.ImageView_baseline, -1);
            mBaselineAlignBottom = a.getBoolean(com.android.internal.R.styleable.ImageView_baselineAlignBottom, false);
            setAdjustViewBounds(a.getBoolean(com.android.internal.R.styleable.ImageView_adjustViewBounds, false));
            setMaxWidth(a.getDimensionPixelSize(com.android.internal.R.styleable.ImageView_maxWidth, Integer.MAX_VALUE));
            setMaxHeight(a.getDimensionPixelSize(com.android.internal.R.styleable.ImageView_maxHeight, Integer.MAX_VALUE));
            int scale = a.getInt(com.android.internal.R.styleable.ImageView_scaleType, -1);
            if (scale >= 0) setScaleType(ScaleType.values()[scale]);
            if (a.hasValue(com.android.internal.R.styleable.ImageView_tint)) {
                setImageTintMode(android.graphics.PorterDuff.Mode.SRC_ATOP);
                setImageTintList(a.getColorStateList(com.android.internal.R.styleable.ImageView_tint));
            }
            if (a.hasValue(com.android.internal.R.styleable.ImageView_tintMode)) {
                setImageTintMode(Drawable.parseTintMode(a.getInt(
                        com.android.internal.R.styleable.ImageView_tintMode, -1), mTintMode));
            }
            setCropToPadding(a.getBoolean(com.android.internal.R.styleable.ImageView_cropToPadding, false));
        } finally {
            a.recycle();
        }
    }
    public void setImageDrawable(Drawable drawable) {
        if (mDrawable == drawable) return;
        final int oldW = mDrawable != null ? mDrawable.getIntrinsicWidth() : -1;
        final int oldH = mDrawable != null ? mDrawable.getIntrinsicHeight() : -1;
        if (mDrawable != null) mDrawable.setCallback(null);
        mDrawable = drawable;
        if (drawable != null) {
            drawable.setCallback(this);
            drawable.setVisible(getVisibility() == VISIBLE, false);
            if (mImageAlpha != 255) drawable.setAlpha(mImageAlpha);
            if (mTintList != null) drawable.setTintList(mTintList);
            if (mTintMode != null) drawable.setTintMode(mTintMode);
            if (mColorFilter != null) drawable.setColorFilter(mColorFilter);
            if (drawable.isStateful()) drawable.setState(getDrawableState());
        }
        if (drawable == null || drawable.getIntrinsicWidth() != oldW || drawable.getIntrinsicHeight() != oldH) {
            requestLayout();
        }
        invalidate();
    }
    public Drawable getDrawable() { return mDrawable; }
    /** Loads the drawable when the resource is one the framework can build; else keeps none. */
    public void setImageResource(int resId) {
        Drawable d = null;
        if (resId != 0 && getContext() != null) {
            try { d = getContext().getDrawable(resId); } catch (RuntimeException ignored) {}
        }
        setImageDrawable(d);
    }
    public void setImageBitmap(Bitmap bm) {
        setImageDrawable(bm != null ? new android.graphics.drawable.BitmapDrawable(getResources(), bm) : null);
    }
    public void setImageURI(Uri uri) { invalidate(); }
    public void setScaleType(ScaleType scaleType) {
        if (scaleType == null) throw new NullPointerException();
        if (mScaleType != scaleType) {
            mScaleType = scaleType;
            requestLayout();
            invalidate();
        }
    }
    private android.graphics.Matrix mMatrix = new android.graphics.Matrix();
    public void setImageMatrix(android.graphics.Matrix matrix) {
        mMatrix = matrix != null ? matrix : new android.graphics.Matrix();
        invalidate();
    }
    public android.graphics.Matrix getImageMatrix() { return mMatrix; }
    public void setImageLevel(int level) {
        if (mDrawable != null) mDrawable.setLevel(level);
        invalidate();
    }
    public ScaleType getScaleType() { return mScaleType; }
    private boolean mAdjustViewBounds;
    private int mMaxWidth = Integer.MAX_VALUE;
    private int mMaxHeight = Integer.MAX_VALUE;
    private boolean mCropToPadding;

    public void setAdjustViewBounds(boolean adjustViewBounds) {
        mAdjustViewBounds = adjustViewBounds;
        requestLayout();
    }
    public void setMaxWidth(int maxWidth) { mMaxWidth = maxWidth; requestLayout(); }
    public void setMaxHeight(int maxHeight) { mMaxHeight = maxHeight; requestLayout(); }
    public final void setColorFilter(int color) {
        setColorFilter(color, android.graphics.PorterDuff.Mode.SRC_ATOP);
    }
    public final void clearColorFilter() { setColorFilter(null); }

    private android.content.res.ColorStateList mTintList;
    private android.graphics.PorterDuff.Mode mTintMode;
    private int mImageAlpha = 255;
    private android.graphics.ColorFilter mColorFilter;

    public void setImageTintList(android.content.res.ColorStateList tint) {
        mTintList = tint;
        if (mDrawable != null) {
            mDrawable.setTintList(tint);
            if (mDrawable.isStateful()) mDrawable.setState(getDrawableState());
        }
        invalidate();
    }

    public android.content.res.ColorStateList getImageTintList() { return mTintList; }

    public void setImageTintMode(android.graphics.PorterDuff.Mode tintMode) {
        mTintMode = tintMode;
        if (mDrawable != null) {
            mDrawable.setTintMode(tintMode);
            if (mDrawable.isStateful()) mDrawable.setState(getDrawableState());
        }
        invalidate();
    }

    public android.graphics.PorterDuff.Mode getImageTintMode() { return mTintMode; }

    public void setImageTintBlendMode(android.graphics.BlendMode blendMode) {}

    public void setColorFilter(android.graphics.ColorFilter cf) {
        mColorFilter = cf;
        if (mDrawable != null) mDrawable.setColorFilter(cf);
        invalidate();
    }

    public void setColorFilter(int color, android.graphics.PorterDuff.Mode mode) {
        setColorFilter(new android.graphics.PorterDuffColorFilter(color, mode));
    }

    public android.graphics.ColorFilter getColorFilter() { return mColorFilter; }

    public void setImageAlpha(int alpha) {
        mImageAlpha = alpha & 0xFF;
        if (mDrawable != null) mDrawable.setAlpha(mImageAlpha);
        invalidate();
    }

    public int getImageAlpha() { return mImageAlpha; }
    public void setCropToPadding(boolean cropToPadding) { mCropToPadding = cropToPadding; invalidate(); }
    public boolean getCropToPadding() { return mCropToPadding; }
    public int getMaxWidth() { return mMaxWidth; }
    public int getMaxHeight() { return mMaxHeight; }
    public boolean getAdjustViewBounds() { return mAdjustViewBounds; }
    private int mBaseline = -1;
    private boolean mBaselineAlignBottom;
    @Override public int getBaseline() { return mBaselineAlignBottom ? getMeasuredHeight() : mBaseline; }
    public void setBaseline(int baseline) {
        if (mBaseline != baseline) { mBaseline = baseline; requestLayout(); }
    }
    public void setBaselineAlignBottom(boolean aligned) {
        if (mBaselineAlignBottom != aligned) { mBaselineAlignBottom = aligned; requestLayout(); }
    }
    public boolean getBaselineAlignBottom() { return mBaselineAlignBottom; }
    public boolean isOpaque() { return false; }

    @Override
    protected boolean verifyDrawable(Drawable who) {
        return who == mDrawable || super.verifyDrawable(who);
    }

    @Override
    public void invalidateDrawable(Drawable dr) {
        if (dr == mDrawable) {
            invalidate();
        } else {
            super.invalidateDrawable(dr);
        }
    }

    @Override
    protected void drawableStateChanged() {
        super.drawableStateChanged();
        // Runs from the View constructor, before this class's fields exist.
        if (mDrawable != null && mDrawable.isStateful()) mDrawable.setState(getDrawableState());
    }

    private static int clampSize(int desired, int max, int measureSpec) {
        final int mode = MeasureSpec.getMode(measureSpec);
        final int size = MeasureSpec.getSize(measureSpec);
        int result = Math.min(desired, max);
        if (mode == MeasureSpec.EXACTLY) return size;
        if (mode == MeasureSpec.AT_MOST) result = Math.min(result, size);
        return result;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int dw = 0;
        int dh = 0;
        if (mDrawable != null) {
            dw = Math.max(0, mDrawable.getIntrinsicWidth());
            dh = Math.max(0, mDrawable.getIntrinsicHeight());
        }
        final int padH = getPaddingLeft() + getPaddingRight();
        final int padV = getPaddingTop() + getPaddingBottom();
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        final boolean resizeWidth = widthMode != MeasureSpec.EXACTLY;
        final boolean resizeHeight = heightMode != MeasureSpec.EXACTLY;

        int w = resizeWidth ? dw + padH : MeasureSpec.getSize(widthMeasureSpec);
        int h = resizeHeight ? dh + padV : MeasureSpec.getSize(heightMeasureSpec);
        if (resizeWidth) w = Math.max(w, getSuggestedMinimumWidth());
        if (resizeHeight) h = Math.max(h, getSuggestedMinimumHeight());
        if (resizeWidth) w = clampSize(w, mMaxWidth, widthMeasureSpec);
        if (resizeHeight) h = clampSize(h, mMaxHeight, heightMeasureSpec);

        // Preserve aspect ratio while honoring both size constraints.
        if (mAdjustViewBounds && dw > 0 && dh > 0 && (resizeWidth || resizeHeight)) {
            boolean adjusted = false;
            if (resizeWidth) {
                int newWidth = (int) ((double) Math.max(0, h - padV) * dw / dh) + padH;
                if (!resizeHeight) w = clampSize(newWidth, mMaxWidth, widthMeasureSpec);
                if (newWidth <= w) { w = newWidth; adjusted = true; }
            }
            if (!adjusted && resizeHeight) {
                int newHeight = (int) ((double) Math.max(0, w - padH) * dh / dw) + padV;
                if (!resizeWidth) h = clampSize(newHeight, mMaxHeight, heightMeasureSpec);
                if (newHeight <= h) h = newHeight;
            }
        }
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onDraw(android.graphics.Canvas canvas) {
        super.onDraw(canvas);
        final Drawable d = mDrawable;
        if (d == null || canvas == null) return;
        final int vw = getWidth() - getPaddingLeft() - getPaddingRight();
        final int vh = getHeight() - getPaddingTop() - getPaddingBottom();
        if (vw <= 0 || vh <= 0) return;
        final int dw = d.getIntrinsicWidth();
        final int dh = d.getIntrinsicHeight();

        // Destination rectangle of the drawable inside the content box.
        float left = 0f;
        float top = 0f;
        float width = vw;
        float height = vh;
        android.graphics.Matrix matrix = null;
        if (dw > 0 && dh > 0) {
            final float fit = Math.min((float) vw / dw, (float) vh / dh);
            final float fill = Math.max((float) vw / dw, (float) vh / dh);
            switch (mScaleType) {
                case MATRIX:
                    matrix = mMatrix;
                    width = dw;
                    height = dh;
                    break;
                case FIT_XY:
                    break;
                case CENTER:
                    width = dw;
                    height = dh;
                    left = (vw - width) / 2f;
                    top = (vh - height) / 2f;
                    break;
                case CENTER_CROP:
                    width = dw * fill;
                    height = dh * fill;
                    left = (vw - width) / 2f;
                    top = (vh - height) / 2f;
                    break;
                case CENTER_INSIDE: {
                    final float scale = Math.min(1f, fit);
                    width = dw * scale;
                    height = dh * scale;
                    left = (vw - width) / 2f;
                    top = (vh - height) / 2f;
                    break;
                }
                case FIT_START:
                    width = dw * fit;
                    height = dh * fit;
                    break;
                case FIT_END:
                    width = dw * fit;
                    height = dh * fit;
                    left = vw - width;
                    top = vh - height;
                    break;
                case FIT_CENTER:
                default:
                    width = dw * fit;
                    height = dh * fit;
                    left = (vw - width) / 2f;
                    top = (vh - height) / 2f;
                    break;
            }
        }

        final int save = canvas.save();
        canvas.translate(getPaddingLeft(), getPaddingTop());
        if (mCropToPadding || mScaleType == ScaleType.CENTER_CROP) {
            canvas.clipRect(0, 0, vw, vh);
        }
        if (matrix != null) canvas.concat(matrix);
        canvas.translate(Math.round(left), Math.round(top));
        final android.graphics.Rect old = d.copyBounds();
        d.setBounds(0, 0, Math.max(1, Math.round(width)), Math.max(1, Math.round(height)));
        d.draw(canvas);
        d.setBounds(old);
        canvas.restoreToCount(save);
    }
}
