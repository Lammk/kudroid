package android.graphics.drawable;

import android.graphics.Rect;

/** Draws another drawable inside its bounds, shrunk by fixed or fractional insets. */
public class InsetDrawable extends DrawableWrapper {
    private int mLeft, mTop, mRight, mBottom;
    private float mLeftFraction, mTopFraction, mRightFraction, mBottomFraction;

    public InsetDrawable(Drawable drawable, int inset) {
        this(drawable, inset, inset, inset, inset);
    }

    public InsetDrawable(Drawable drawable, float inset) {
        this(drawable, inset, inset, inset, inset);
    }

    public InsetDrawable(Drawable drawable, int insetLeft, int insetTop, int insetRight,
            int insetBottom) {
        super(drawable);
        mLeft = insetLeft;
        mTop = insetTop;
        mRight = insetRight;
        mBottom = insetBottom;
    }

    public InsetDrawable(Drawable drawable, float insetLeftFraction, float insetTopFraction,
            float insetRightFraction, float insetBottomFraction) {
        super(drawable);
        mLeftFraction = insetLeftFraction;
        mTopFraction = insetTopFraction;
        mRightFraction = insetRightFraction;
        mBottomFraction = insetBottomFraction;
    }

    private int insetLeft(Rect b) { return mLeft + (int) (b.width() * mLeftFraction); }
    private int insetTop(Rect b) { return mTop + (int) (b.height() * mTopFraction); }
    private int insetRight(Rect b) { return mRight + (int) (b.width() * mRightFraction); }
    private int insetBottom(Rect b) { return mBottom + (int) (b.height() * mBottomFraction); }

    @Override
    protected void onBoundsChange(Rect bounds) {
        final Drawable d = getDrawable();
        if (d == null) return;
        d.setBounds(bounds.left + insetLeft(bounds), bounds.top + insetTop(bounds),
                bounds.right - insetRight(bounds), bounds.bottom - insetBottom(bounds));
    }

    @Override
    public int getIntrinsicWidth() {
        final int w = super.getIntrinsicWidth();
        return w < 0 ? -1 : w + mLeft + mRight;
    }

    @Override
    public int getIntrinsicHeight() {
        final int h = super.getIntrinsicHeight();
        return h < 0 ? -1 : h + mTop + mBottom;
    }

    @Override
    public boolean getPadding(Rect padding) {
        final boolean hasPadding = super.getPadding(padding);
        padding.left += mLeft;
        padding.top += mTop;
        padding.right += mRight;
        padding.bottom += mBottom;
        return hasPadding || (mLeft | mTop | mRight | mBottom) != 0;
    }
}
