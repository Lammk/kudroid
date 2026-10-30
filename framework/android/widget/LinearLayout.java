package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;

/**
 * Arranges children in a single row or column, honouring layout_width/height, margins, weights,
 * gravity, dividers and baseline alignment the way Android's LinearLayout does.
 */
public class LinearLayout extends ViewGroup {
    public static final int HORIZONTAL = 0;
    public static final int VERTICAL = 1;

    public static final int SHOW_DIVIDER_NONE = 0;
    public static final int SHOW_DIVIDER_BEGINNING = 1;
    public static final int SHOW_DIVIDER_MIDDLE = 2;
    public static final int SHOW_DIVIDER_END = 4;

    private static final int VERTICAL_GRAVITY_COUNT = 4;
    private static final int INDEX_CENTER_VERTICAL = 0;
    private static final int INDEX_TOP = 1;
    private static final int INDEX_BOTTOM = 2;
    private static final int INDEX_FILL = 3;

    private boolean mBaselineAligned = true;
    private int mBaselineAlignedChildIndex = -1;
    private int mBaselineChildTop = 0;
    private int mOrientation = HORIZONTAL;
    private int mGravity = Gravity.START | Gravity.TOP;
    private int mTotalLength;
    private float mWeightSum = -1.0f;
    private boolean mUseLargestChild;
    private int[] mMaxAscent;
    private int[] mMaxDescent;
    private Drawable mDivider;
    private int mDividerWidth;
    private int mDividerHeight;
    private int mShowDividers;
    private int mDividerPadding;

    public LinearLayout(Context context) {
        this(context, null);
    }

    public LinearLayout(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public LinearLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public LinearLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (context != null) {
            final android.content.res.TypedArray a = context.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.LinearLayout, defStyleAttr, defStyleRes);
            try {
                final int index = a.getInt(com.android.internal.R.styleable.LinearLayout_orientation, -1);
                if (index >= 0) mOrientation = index;
                final int gravity = a.getInt(com.android.internal.R.styleable.LinearLayout_gravity, -1);
                if (gravity >= 0) setGravity(gravity);
                mBaselineAligned = a.getBoolean(
                        com.android.internal.R.styleable.LinearLayout_baselineAligned, true);
                mBaselineAlignedChildIndex = a.getInt(
                        com.android.internal.R.styleable.LinearLayout_baselineAlignedChildIndex, -1);
                mWeightSum = a.getFloat(com.android.internal.R.styleable.LinearLayout_weightSum, -1.0f);
                mUseLargestChild = a.getBoolean(
                        com.android.internal.R.styleable.LinearLayout_measureWithLargestChild, false);
                mShowDividers = a.getInt(com.android.internal.R.styleable.LinearLayout_showDividers,
                        SHOW_DIVIDER_NONE);
                mDividerPadding = a.getDimensionPixelSize(
                        com.android.internal.R.styleable.LinearLayout_dividerPadding, 0);
                setDividerDrawable(a.getDrawable(com.android.internal.R.styleable.LinearLayout_divider));
            } finally {
                a.recycle();
            }
        }
    }

    public void setShowDividers(int showDividers) {
        if (showDividers == mShowDividers) return;
        mShowDividers = showDividers;
        setWillNotDraw(!isShowingDividers());
        requestLayout();
    }

    public int getShowDividers() { return mShowDividers; }

    public Drawable getDividerDrawable() { return mDivider; }

    public void setDividerDrawable(Drawable divider) {
        if (divider == mDivider) return;
        mDivider = divider;
        if (divider != null) {
            mDividerWidth = divider.getIntrinsicWidth();
            mDividerHeight = divider.getIntrinsicHeight();
        } else {
            mDividerWidth = 0;
            mDividerHeight = 0;
        }
        setWillNotDraw(!isShowingDividers());
        requestLayout();
    }

    public void setDividerPadding(int padding) {
        if (padding == mDividerPadding) return;
        mDividerPadding = padding;
        if (isShowingDividers()) {
            requestLayout();
            invalidate();
        }
    }

    public int getDividerPadding() { return mDividerPadding; }

    public int getDividerWidth() { return mDividerWidth; }

    private boolean isShowingDividers() {
        return mShowDividers != SHOW_DIVIDER_NONE && mDivider != null;
    }

    public boolean isMeasureWithLargestChildEnabled() { return mUseLargestChild; }

    public void setMeasureWithLargestChildEnabled(boolean enabled) { mUseLargestChild = enabled; }

    public float getWeightSum() { return mWeightSum; }
    public void setWeightSum(float weightSum) { mWeightSum = Math.max(0.0f, weightSum); }
    public boolean isBaselineAligned() { return mBaselineAligned; }
    public void setBaselineAligned(boolean baselineAligned) { mBaselineAligned = baselineAligned; }
    public int getBaselineAlignedChildIndex() { return mBaselineAlignedChildIndex; }

    public void setBaselineAlignedChildIndex(int i) {
        if (i < 0 || i >= getChildCount()) {
            throw new IllegalArgumentException("base aligned child index out of range (0, "
                    + getChildCount() + ")");
        }
        mBaselineAlignedChildIndex = i;
    }

    View getVirtualChildAt(int index) { return getChildAt(index); }

    int getVirtualChildCount() { return getChildCount(); }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        if (mOrientation == HORIZONTAL) {
            return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
        } else if (mOrientation == VERTICAL) {
            return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        }
        return null;
    }

    @Override
    public ViewGroup.LayoutParams generateLayoutParams(android.util.AttributeSet attrs) {
        return new LayoutParams(getContext(), attrs);
    }

    @Override
    protected ViewGroup.LayoutParams generateLayoutParams(ViewGroup.LayoutParams p) {
        if (p instanceof LayoutParams) return new LayoutParams((LayoutParams) p);
        if (p instanceof ViewGroup.MarginLayoutParams) return new LayoutParams((ViewGroup.MarginLayoutParams) p);
        return new LayoutParams(p);
    }

    @Override
    protected boolean checkLayoutParams(ViewGroup.LayoutParams p) {
        return p instanceof LayoutParams;
    }

    public void setOrientation(int orientation) {
        if (mOrientation != orientation) {
            mOrientation = orientation;
            requestLayout();
        }
    }

    public int getOrientation() {
        return mOrientation;
    }

    public void setGravity(int gravity) {
        if (mGravity != gravity) {
            if ((gravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK) == 0) {
                gravity |= Gravity.START;
            }
            if ((gravity & Gravity.VERTICAL_GRAVITY_MASK) == 0) {
                gravity |= Gravity.TOP;
            }
            mGravity = gravity;
            requestLayout();
        }
    }

    public int getGravity() {
        return mGravity;
    }

    public void setHorizontalGravity(int horizontalGravity) {
        final int gravity = horizontalGravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK;
        if ((mGravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK) != gravity) {
            mGravity = (mGravity & ~Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK) | gravity;
            requestLayout();
        }
    }

    public void setVerticalGravity(int verticalGravity) {
        final int gravity = verticalGravity & Gravity.VERTICAL_GRAVITY_MASK;
        if ((mGravity & Gravity.VERTICAL_GRAVITY_MASK) != gravity) {
            mGravity = (mGravity & ~Gravity.VERTICAL_GRAVITY_MASK) | gravity;
            requestLayout();
        }
    }

    // ---- Dividers.

    protected boolean hasDividerBeforeChildAt(int childIndex) {
        if (childIndex == getVirtualChildCount()) {
            return (mShowDividers & SHOW_DIVIDER_END) != 0;
        }
        final boolean allViewsAreGoneBefore = allViewsAreGoneBefore(childIndex);
        if (allViewsAreGoneBefore) {
            return (mShowDividers & SHOW_DIVIDER_BEGINNING) != 0;
        }
        return (mShowDividers & SHOW_DIVIDER_MIDDLE) != 0;
    }

    private boolean allViewsAreGoneBefore(int childIndex) {
        for (int i = childIndex - 1; i >= 0; i--) {
            final View child = getVirtualChildAt(i);
            if (child != null && child.getVisibility() != GONE) return false;
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (mDivider == null) return;
        if (mOrientation == VERTICAL) {
            drawDividersVertical(canvas);
        } else {
            drawDividersHorizontal(canvas);
        }
    }

    void drawDividersVertical(Canvas canvas) {
        final int count = getVirtualChildCount();
        for (int i = 0; i < count; i++) {
            final View child = getVirtualChildAt(i);
            if (child != null && child.getVisibility() != GONE && hasDividerBeforeChildAt(i)) {
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                drawHorizontalDivider(canvas, child.getTop() - lp.topMargin - mDividerHeight);
            }
        }
        if (hasDividerBeforeChildAt(count)) {
            final View child = getLastNonGoneChild();
            final int bottom;
            if (child == null) {
                bottom = getHeight() - getPaddingBottom() - mDividerHeight;
            } else {
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                bottom = child.getBottom() + lp.bottomMargin;
            }
            drawHorizontalDivider(canvas, bottom);
        }
    }

    void drawDividersHorizontal(Canvas canvas) {
        final int count = getVirtualChildCount();
        for (int i = 0; i < count; i++) {
            final View child = getVirtualChildAt(i);
            if (child != null && child.getVisibility() != GONE && hasDividerBeforeChildAt(i)) {
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                drawVerticalDivider(canvas, child.getLeft() - lp.leftMargin - mDividerWidth);
            }
        }
        if (hasDividerBeforeChildAt(count)) {
            final View child = getLastNonGoneChild();
            final int right;
            if (child == null) {
                right = getWidth() - getPaddingRight() - mDividerWidth;
            } else {
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                right = child.getRight() + lp.rightMargin;
            }
            drawVerticalDivider(canvas, right);
        }
    }

    private View getLastNonGoneChild() {
        for (int i = getVirtualChildCount() - 1; i >= 0; i--) {
            final View child = getVirtualChildAt(i);
            if (child != null && child.getVisibility() != GONE) return child;
        }
        return null;
    }

    void drawHorizontalDivider(Canvas canvas, int top) {
        mDivider.setBounds(getPaddingLeft() + mDividerPadding, top,
                getWidth() - getPaddingRight() - mDividerPadding, top + mDividerHeight);
        mDivider.draw(canvas);
    }

    void drawVerticalDivider(Canvas canvas, int left) {
        mDivider.setBounds(left, getPaddingTop() + mDividerPadding, left + mDividerWidth,
                getHeight() - getPaddingBottom() - mDividerPadding);
        mDivider.draw(canvas);
    }

    // ---- Baseline.

    @Override
    public int getBaseline() {
        if (mBaselineAlignedChildIndex < 0) {
            return super.getBaseline();
        }
        if (getChildCount() <= mBaselineAlignedChildIndex) {
            throw new RuntimeException("mBaselineAlignedChildIndex of LinearLayout "
                    + "set to an index that is out of bounds.");
        }
        final View child = getChildAt(mBaselineAlignedChildIndex);
        final int childBaseline = child.getBaseline();
        if (childBaseline == -1) {
            if (mBaselineAlignedChildIndex == 0) return -1;
            throw new RuntimeException("mBaselineAlignedChildIndex of LinearLayout "
                    + "points to a View that doesn't know how to get its baseline.");
        }
        int childTop = mBaselineChildTop;
        if (mOrientation == VERTICAL) {
            final int majorGravity = mGravity & Gravity.VERTICAL_GRAVITY_MASK;
            if (majorGravity != Gravity.TOP) {
                switch (majorGravity) {
                    case Gravity.BOTTOM:
                        childTop = getBottom() - getTop() - getPaddingBottom() - mTotalLength;
                        break;
                    case Gravity.CENTER_VERTICAL:
                        childTop += ((getBottom() - getTop() - getPaddingTop() - getPaddingBottom())
                                - mTotalLength) / 2;
                        break;
                }
            }
        }
        final LayoutParams lp = (LayoutParams) child.getLayoutParams();
        return childTop + lp.topMargin + childBaseline;
    }

    // ---- Measure.

    int measureNullChild(int childIndex) { return 0; }

    void measureChildBeforeLayout(View child, int childIndex, int widthMeasureSpec, int totalWidth,
            int heightMeasureSpec, int totalHeight) {
        measureChildWithMargins(child, widthMeasureSpec, totalWidth, heightMeasureSpec, totalHeight);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        if (mOrientation == VERTICAL) {
            measureVertical(widthMeasureSpec, heightMeasureSpec);
        } else {
            measureHorizontal(widthMeasureSpec, heightMeasureSpec);
        }
    }

    void measureVertical(int widthMeasureSpec, int heightMeasureSpec) {
        mTotalLength = 0;
        int maxWidth = 0;
        int childState = 0;
        int alternativeMaxWidth = 0;
        int weightedMaxWidth = 0;
        boolean allFillParent = true;
        float totalWeight = 0;

        final int count = getVirtualChildCount();
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);

        boolean matchWidth = false;
        boolean skippedMeasure = false;
        final boolean useLargestChild = mUseLargestChild;
        int largestChildHeight = Integer.MIN_VALUE;
        int consumedExcessSpace = 0;
        int nonSkippedChildCount = 0;

        for (int i = 0; i < count; ++i) {
            final View child = getVirtualChildAt(i);
            if (child == null) {
                mTotalLength += measureNullChild(i);
                continue;
            }
            if (child.getVisibility() == View.GONE) continue;
            nonSkippedChildCount++;
            if (hasDividerBeforeChildAt(i)) mTotalLength += mDividerHeight;

            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            totalWeight += lp.weight;
            final boolean useExcessSpace = lp.height == 0 && lp.weight > 0;
            if (heightMode == MeasureSpec.EXACTLY && useExcessSpace) {
                final int totalLength = mTotalLength;
                mTotalLength = Math.max(totalLength, totalLength + lp.topMargin + lp.bottomMargin);
                skippedMeasure = true;
            } else {
                if (useExcessSpace) {
                    // Only laid out from excess space: measure as WRAP_CONTENT, restore 0 after.
                    lp.height = LayoutParams.WRAP_CONTENT;
                }
                final int usedHeight = totalWeight == 0 ? mTotalLength : 0;
                measureChildBeforeLayout(child, i, widthMeasureSpec, 0, heightMeasureSpec, usedHeight);
                final int childHeight = child.getMeasuredHeight();
                if (useExcessSpace) {
                    lp.height = 0;
                    consumedExcessSpace += childHeight;
                }
                final int totalLength = mTotalLength;
                mTotalLength = Math.max(totalLength,
                        totalLength + childHeight + lp.topMargin + lp.bottomMargin);
                if (useLargestChild) largestChildHeight = Math.max(childHeight, largestChildHeight);
            }

            boolean matchWidthLocally = false;
            if (widthMode != MeasureSpec.EXACTLY && lp.width == LayoutParams.MATCH_PARENT) {
                matchWidth = true;
                matchWidthLocally = true;
            }
            final int margin = lp.leftMargin + lp.rightMargin;
            final int measuredWidth = child.getMeasuredWidth() + margin;
            maxWidth = Math.max(maxWidth, measuredWidth);
            childState = combineMeasuredStates(childState, child.getMeasuredState());
            allFillParent = allFillParent && lp.width == LayoutParams.MATCH_PARENT;
            if (lp.weight > 0) {
                weightedMaxWidth = Math.max(weightedMaxWidth, matchWidthLocally ? margin : measuredWidth);
            } else {
                alternativeMaxWidth = Math.max(alternativeMaxWidth,
                        matchWidthLocally ? margin : measuredWidth);
            }
        }

        if (nonSkippedChildCount > 0 && hasDividerBeforeChildAt(count)) mTotalLength += mDividerHeight;

        if (useLargestChild && (heightMode == MeasureSpec.AT_MOST || heightMode == MeasureSpec.UNSPECIFIED)) {
            mTotalLength = 0;
            for (int i = 0; i < count; ++i) {
                final View child = getVirtualChildAt(i);
                if (child == null) {
                    mTotalLength += measureNullChild(i);
                    continue;
                }
                if (child.getVisibility() == GONE) continue;
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                final int totalLength = mTotalLength;
                mTotalLength = Math.max(totalLength,
                        totalLength + largestChildHeight + lp.topMargin + lp.bottomMargin);
            }
        }

        mTotalLength += getPaddingTop() + getPaddingBottom();
        int heightSize = mTotalLength;
        heightSize = Math.max(heightSize, getSuggestedMinimumHeight());
        final int heightSizeAndState = resolveSizeAndState(heightSize, heightMeasureSpec, 0);
        heightSize = heightSizeAndState & MEASURED_SIZE_MASK;

        int remainingExcess = heightSize - mTotalLength + consumedExcessSpace;
        if (skippedMeasure || (remainingExcess != 0 && totalWeight > 0.0f)) {
            float remainingWeightSum = mWeightSum > 0.0f ? mWeightSum : totalWeight;
            mTotalLength = 0;
            for (int i = 0; i < count; ++i) {
                final View child = getVirtualChildAt(i);
                if (child == null || child.getVisibility() == View.GONE) continue;
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                final float childWeight = lp.weight;
                if (childWeight > 0) {
                    final int share = (int) (childWeight * remainingExcess / remainingWeightSum);
                    remainingExcess -= share;
                    remainingWeightSum -= childWeight;
                    final int childHeight;
                    if (mUseLargestChild && heightMode != MeasureSpec.EXACTLY) {
                        childHeight = largestChildHeight;
                    } else if (lp.height == 0) {
                        childHeight = share;
                    } else {
                        childHeight = child.getMeasuredHeight() + share;
                    }
                    final int childHeightMeasureSpec = MeasureSpec.makeMeasureSpec(
                            Math.max(0, childHeight), MeasureSpec.EXACTLY);
                    final int childWidthMeasureSpec = getChildMeasureSpec(widthMeasureSpec,
                            getPaddingLeft() + getPaddingRight() + lp.leftMargin + lp.rightMargin, lp.width);
                    child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
                    childState = combineMeasuredStates(childState,
                            child.getMeasuredState() & (MEASURED_STATE_MASK >> MEASURED_HEIGHT_STATE_SHIFT));
                }
                final int margin = lp.leftMargin + lp.rightMargin;
                final int measuredWidth = child.getMeasuredWidth() + margin;
                maxWidth = Math.max(maxWidth, measuredWidth);
                final boolean matchWidthLocally = widthMode != MeasureSpec.EXACTLY
                        && lp.width == LayoutParams.MATCH_PARENT;
                alternativeMaxWidth = Math.max(alternativeMaxWidth, matchWidthLocally ? margin : measuredWidth);
                allFillParent = allFillParent && lp.width == LayoutParams.MATCH_PARENT;
                final int totalLength = mTotalLength;
                mTotalLength = Math.max(totalLength,
                        totalLength + child.getMeasuredHeight() + lp.topMargin + lp.bottomMargin);
            }
            mTotalLength += getPaddingTop() + getPaddingBottom();
        } else {
            alternativeMaxWidth = Math.max(alternativeMaxWidth, weightedMaxWidth);
            if (useLargestChild && heightMode != MeasureSpec.EXACTLY) {
                for (int i = 0; i < count; i++) {
                    final View child = getVirtualChildAt(i);
                    if (child == null || child.getVisibility() == View.GONE) continue;
                    final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                    if (lp.weight > 0) {
                        child.measure(
                                MeasureSpec.makeMeasureSpec(child.getMeasuredWidth(), MeasureSpec.EXACTLY),
                                MeasureSpec.makeMeasureSpec(largestChildHeight, MeasureSpec.EXACTLY));
                    }
                }
            }
        }

        if (!allFillParent && widthMode != MeasureSpec.EXACTLY) maxWidth = alternativeMaxWidth;
        maxWidth += getPaddingLeft() + getPaddingRight();
        maxWidth = Math.max(maxWidth, getSuggestedMinimumWidth());
        setMeasuredDimension(resolveSizeAndState(maxWidth, widthMeasureSpec, childState), heightSizeAndState);
        if (matchWidth) forceUniformWidth(count, heightMeasureSpec);
    }

    private void forceUniformWidth(int count, int heightMeasureSpec) {
        final int uniformMeasureSpec = MeasureSpec.makeMeasureSpec(getMeasuredWidth(), MeasureSpec.EXACTLY);
        for (int i = 0; i < count; ++i) {
            final View child = getVirtualChildAt(i);
            if (child != null && child.getVisibility() != GONE) {
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                if (lp.width == LayoutParams.MATCH_PARENT) {
                    // Temporarily pin the height so the child is not re-measured vertically.
                    final int oldHeight = lp.height;
                    lp.height = child.getMeasuredHeight();
                    measureChildWithMargins(child, uniformMeasureSpec, 0, heightMeasureSpec, 0);
                    lp.height = oldHeight;
                }
            }
        }
    }

    private void forceUniformHeight(int count, int widthMeasureSpec) {
        final int uniformMeasureSpec = MeasureSpec.makeMeasureSpec(getMeasuredHeight(), MeasureSpec.EXACTLY);
        for (int i = 0; i < count; ++i) {
            final View child = getVirtualChildAt(i);
            if (child != null && child.getVisibility() != GONE) {
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                if (lp.height == LayoutParams.MATCH_PARENT) {
                    final int oldWidth = lp.width;
                    lp.width = child.getMeasuredWidth();
                    measureChildWithMargins(child, widthMeasureSpec, 0, uniformMeasureSpec, 0);
                    lp.width = oldWidth;
                }
            }
        }
    }

    void measureHorizontal(int widthMeasureSpec, int heightMeasureSpec) {
        mTotalLength = 0;
        int maxHeight = 0;
        int childState = 0;
        int alternativeMaxHeight = 0;
        int weightedMaxHeight = 0;
        boolean allFillParent = true;
        float totalWeight = 0;

        final int count = getVirtualChildCount();
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);

        boolean matchHeight = false;
        boolean skippedMeasure = false;

        if (mMaxAscent == null || mMaxDescent == null) {
            mMaxAscent = new int[VERTICAL_GRAVITY_COUNT];
            mMaxDescent = new int[VERTICAL_GRAVITY_COUNT];
        }
        final int[] maxAscent = mMaxAscent;
        final int[] maxDescent = mMaxDescent;
        maxAscent[0] = maxAscent[1] = maxAscent[2] = maxAscent[3] = -1;
        maxDescent[0] = maxDescent[1] = maxDescent[2] = maxDescent[3] = -1;

        final boolean baselineAligned = mBaselineAligned;
        final boolean useLargestChild = mUseLargestChild;
        final boolean isExactly = widthMode == MeasureSpec.EXACTLY;
        int largestChildWidth = Integer.MIN_VALUE;
        int usedExcessSpace = 0;
        int nonSkippedChildCount = 0;

        for (int i = 0; i < count; ++i) {
            final View child = getVirtualChildAt(i);
            if (child == null) {
                mTotalLength += measureNullChild(i);
                continue;
            }
            if (child.getVisibility() == GONE) continue;
            nonSkippedChildCount++;
            if (hasDividerBeforeChildAt(i)) mTotalLength += mDividerWidth;

            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            totalWeight += lp.weight;
            final boolean useExcessSpace = lp.width == 0 && lp.weight > 0;
            if (isExactly && useExcessSpace) {
                if (isExactly) {
                    mTotalLength += lp.leftMargin + lp.rightMargin;
                } else {
                    final int totalLength = mTotalLength;
                    mTotalLength = Math.max(totalLength, totalLength + lp.leftMargin + lp.rightMargin);
                }
                // Baseline alignment needs the child measured even when it only uses excess space.
                if (baselineAligned) {
                    final int freeWidthSpec = MeasureSpec.makeMeasureSpec(
                            MeasureSpec.getSize(widthMeasureSpec), MeasureSpec.UNSPECIFIED);
                    final int freeHeightSpec = MeasureSpec.makeMeasureSpec(
                            MeasureSpec.getSize(heightMeasureSpec), MeasureSpec.UNSPECIFIED);
                    child.measure(freeWidthSpec, freeHeightSpec);
                } else {
                    skippedMeasure = true;
                }
            } else {
                if (useExcessSpace) {
                    lp.width = LayoutParams.WRAP_CONTENT;
                }
                final int usedWidth = totalWeight == 0 ? mTotalLength : 0;
                measureChildBeforeLayout(child, i, widthMeasureSpec, usedWidth, heightMeasureSpec, 0);
                final int childWidth = child.getMeasuredWidth();
                if (useExcessSpace) {
                    lp.width = 0;
                    usedExcessSpace += childWidth;
                }
                if (isExactly) {
                    mTotalLength += childWidth + lp.leftMargin + lp.rightMargin;
                } else {
                    final int totalLength = mTotalLength;
                    mTotalLength = Math.max(totalLength, totalLength + childWidth + lp.leftMargin + lp.rightMargin);
                }
                if (useLargestChild) largestChildWidth = Math.max(childWidth, largestChildWidth);
            }

            boolean matchHeightLocally = false;
            if (heightMode != MeasureSpec.EXACTLY && lp.height == LayoutParams.MATCH_PARENT) {
                matchHeight = true;
                matchHeightLocally = true;
            }
            final int margin = lp.topMargin + lp.bottomMargin;
            final int childHeight = child.getMeasuredHeight() + margin;
            childState = combineMeasuredStates(childState, child.getMeasuredState());
            if (baselineAligned) {
                final int childBaseline = child.getBaseline();
                if (childBaseline != -1) {
                    final int gravity = (lp.gravity < 0 ? mGravity : lp.gravity) & Gravity.VERTICAL_GRAVITY_MASK;
                    final int index = ((gravity >> Gravity.AXIS_Y_SHIFT) & ~Gravity.AXIS_SPECIFIED) >> 1;
                    maxAscent[index] = Math.max(maxAscent[index], childBaseline);
                    maxDescent[index] = Math.max(maxDescent[index], childHeight - childBaseline);
                }
            }
            maxHeight = Math.max(maxHeight, childHeight);
            allFillParent = allFillParent && lp.height == LayoutParams.MATCH_PARENT;
            if (lp.weight > 0) {
                weightedMaxHeight = Math.max(weightedMaxHeight, matchHeightLocally ? margin : childHeight);
            } else {
                alternativeMaxHeight = Math.max(alternativeMaxHeight, matchHeightLocally ? margin : childHeight);
            }
        }

        if (nonSkippedChildCount > 0 && hasDividerBeforeChildAt(count)) mTotalLength += mDividerWidth;

        if (maxAscent[INDEX_TOP] != -1 || maxAscent[INDEX_CENTER_VERTICAL] != -1
                || maxAscent[INDEX_BOTTOM] != -1 || maxAscent[INDEX_FILL] != -1) {
            final int ascent = Math.max(maxAscent[INDEX_FILL], Math.max(maxAscent[INDEX_CENTER_VERTICAL],
                    Math.max(maxAscent[INDEX_TOP], maxAscent[INDEX_BOTTOM])));
            final int descent = Math.max(maxDescent[INDEX_FILL], Math.max(maxDescent[INDEX_CENTER_VERTICAL],
                    Math.max(maxDescent[INDEX_TOP], maxDescent[INDEX_BOTTOM])));
            maxHeight = Math.max(maxHeight, ascent + descent);
        }

        if (useLargestChild && (widthMode == MeasureSpec.AT_MOST || widthMode == MeasureSpec.UNSPECIFIED)) {
            mTotalLength = 0;
            for (int i = 0; i < count; ++i) {
                final View child = getVirtualChildAt(i);
                if (child == null) {
                    mTotalLength += measureNullChild(i);
                    continue;
                }
                if (child.getVisibility() == GONE) continue;
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                if (isExactly) {
                    mTotalLength += largestChildWidth + lp.leftMargin + lp.rightMargin;
                } else {
                    final int totalLength = mTotalLength;
                    mTotalLength = Math.max(totalLength,
                            totalLength + largestChildWidth + lp.leftMargin + lp.rightMargin);
                }
            }
        }

        mTotalLength += getPaddingLeft() + getPaddingRight();
        int widthSize = mTotalLength;
        widthSize = Math.max(widthSize, getSuggestedMinimumWidth());
        final int widthSizeAndState = resolveSizeAndState(widthSize, widthMeasureSpec, 0);
        widthSize = widthSizeAndState & MEASURED_SIZE_MASK;

        int remainingExcess = widthSize - mTotalLength + usedExcessSpace;
        if (skippedMeasure || (remainingExcess != 0 && totalWeight > 0.0f)) {
            float remainingWeightSum = mWeightSum > 0.0f ? mWeightSum : totalWeight;
            maxAscent[0] = maxAscent[1] = maxAscent[2] = maxAscent[3] = -1;
            maxDescent[0] = maxDescent[1] = maxDescent[2] = maxDescent[3] = -1;
            maxHeight = -1;
            mTotalLength = 0;

            for (int i = 0; i < count; ++i) {
                final View child = getVirtualChildAt(i);
                if (child == null || child.getVisibility() == View.GONE) continue;
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                final float childWeight = lp.weight;
                if (childWeight > 0) {
                    final int share = (int) (childWeight * remainingExcess / remainingWeightSum);
                    remainingExcess -= share;
                    remainingWeightSum -= childWeight;
                    final int childWidth;
                    if (mUseLargestChild && widthMode != MeasureSpec.EXACTLY) {
                        childWidth = largestChildWidth;
                    } else if (lp.width == 0) {
                        childWidth = share;
                    } else {
                        childWidth = child.getMeasuredWidth() + share;
                    }
                    final int childWidthMeasureSpec = MeasureSpec.makeMeasureSpec(
                            Math.max(0, childWidth), MeasureSpec.EXACTLY);
                    final int childHeightMeasureSpec = getChildMeasureSpec(heightMeasureSpec,
                            getPaddingTop() + getPaddingBottom() + lp.topMargin + lp.bottomMargin, lp.height);
                    child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
                    childState = combineMeasuredStates(childState,
                            child.getMeasuredState() & MEASURED_STATE_MASK);
                }
                if (isExactly) {
                    mTotalLength += child.getMeasuredWidth() + lp.leftMargin + lp.rightMargin;
                } else {
                    final int totalLength = mTotalLength;
                    mTotalLength = Math.max(totalLength,
                            totalLength + child.getMeasuredWidth() + lp.leftMargin + lp.rightMargin);
                }
                final boolean matchHeightLocally = heightMode != MeasureSpec.EXACTLY
                        && lp.height == LayoutParams.MATCH_PARENT;
                final int margin = lp.topMargin + lp.bottomMargin;
                final int childHeight = child.getMeasuredHeight() + margin;
                maxHeight = Math.max(maxHeight, childHeight);
                alternativeMaxHeight = Math.max(alternativeMaxHeight, matchHeightLocally ? margin : childHeight);
                allFillParent = allFillParent && lp.height == LayoutParams.MATCH_PARENT;
                if (baselineAligned) {
                    final int childBaseline = child.getBaseline();
                    if (childBaseline != -1) {
                        final int gravity = (lp.gravity < 0 ? mGravity : lp.gravity)
                                & Gravity.VERTICAL_GRAVITY_MASK;
                        final int index = ((gravity >> Gravity.AXIS_Y_SHIFT) & ~Gravity.AXIS_SPECIFIED) >> 1;
                        maxAscent[index] = Math.max(maxAscent[index], childBaseline);
                        maxDescent[index] = Math.max(maxDescent[index], childHeight - childBaseline);
                    }
                }
            }
            mTotalLength += getPaddingLeft() + getPaddingRight();
            if (maxAscent[INDEX_TOP] != -1 || maxAscent[INDEX_CENTER_VERTICAL] != -1
                    || maxAscent[INDEX_BOTTOM] != -1 || maxAscent[INDEX_FILL] != -1) {
                final int ascent = Math.max(maxAscent[INDEX_FILL], Math.max(maxAscent[INDEX_CENTER_VERTICAL],
                        Math.max(maxAscent[INDEX_TOP], maxAscent[INDEX_BOTTOM])));
                final int descent = Math.max(maxDescent[INDEX_FILL], Math.max(maxDescent[INDEX_CENTER_VERTICAL],
                        Math.max(maxDescent[INDEX_TOP], maxDescent[INDEX_BOTTOM])));
                maxHeight = Math.max(maxHeight, ascent + descent);
            }
        } else {
            alternativeMaxHeight = Math.max(alternativeMaxHeight, weightedMaxHeight);
            if (useLargestChild && widthMode != MeasureSpec.EXACTLY) {
                for (int i = 0; i < count; i++) {
                    final View child = getVirtualChildAt(i);
                    if (child == null || child.getVisibility() == View.GONE) continue;
                    final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                    if (lp.weight > 0) {
                        child.measure(
                                MeasureSpec.makeMeasureSpec(largestChildWidth, MeasureSpec.EXACTLY),
                                MeasureSpec.makeMeasureSpec(child.getMeasuredHeight(), MeasureSpec.EXACTLY));
                    }
                }
            }
        }

        if (!allFillParent && heightMode != MeasureSpec.EXACTLY) maxHeight = alternativeMaxHeight;
        maxHeight += getPaddingTop() + getPaddingBottom();
        maxHeight = Math.max(maxHeight, getSuggestedMinimumHeight());
        setMeasuredDimension(widthSizeAndState | (childState & MEASURED_STATE_MASK),
                resolveSizeAndState(maxHeight, heightMeasureSpec, childState << MEASURED_HEIGHT_STATE_SHIFT));
        if (matchHeight) forceUniformHeight(count, widthMeasureSpec);
    }

    // ---- Layout.

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        if (mOrientation == VERTICAL) {
            layoutVertical(l, t, r, b);
        } else {
            layoutHorizontal(l, t, r, b);
        }
    }

    int getLocationOffset(View child) { return 0; }

    int getNextLocationOffset(View child) { return 0; }

    void layoutVertical(int left, int top, int right, int bottom) {
        final int paddingLeft = getPaddingLeft();
        int childTop;
        int childLeft;
        final int width = right - left;
        final int childRight = width - getPaddingRight();
        final int childSpace = width - paddingLeft - getPaddingRight();
        final int count = getVirtualChildCount();
        final int majorGravity = mGravity & Gravity.VERTICAL_GRAVITY_MASK;
        final int minorGravity = mGravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK;

        switch (majorGravity) {
            case Gravity.BOTTOM:
                childTop = getPaddingTop() + bottom - top - mTotalLength;
                break;
            case Gravity.CENTER_VERTICAL:
                childTop = getPaddingTop() + (bottom - top - mTotalLength) / 2;
                break;
            case Gravity.TOP:
            default:
                childTop = getPaddingTop();
                break;
        }

        for (int i = 0; i < count; i++) {
            final View child = getVirtualChildAt(i);
            if (child == null) {
                childTop += measureNullChild(i);
            } else if (child.getVisibility() != GONE) {
                final int childWidth = child.getMeasuredWidth();
                final int childHeight = child.getMeasuredHeight();
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                int gravity = lp.gravity;
                if (gravity < 0) gravity = minorGravity;
                final int absoluteGravity = Gravity.getAbsoluteGravity(gravity, getLayoutDirection());
                switch (absoluteGravity & Gravity.HORIZONTAL_GRAVITY_MASK) {
                    case Gravity.CENTER_HORIZONTAL:
                        childLeft = paddingLeft + ((childSpace - childWidth) / 2) + lp.leftMargin - lp.rightMargin;
                        break;
                    case Gravity.RIGHT:
                        childLeft = childRight - childWidth - lp.rightMargin;
                        break;
                    case Gravity.LEFT:
                    default:
                        childLeft = paddingLeft + lp.leftMargin;
                        break;
                }
                if (hasDividerBeforeChildAt(i)) childTop += mDividerHeight;
                childTop += lp.topMargin;
                child.layout(childLeft, childTop + getLocationOffset(child), childLeft + childWidth,
                        childTop + getLocationOffset(child) + childHeight);
                childTop += childHeight + lp.bottomMargin + getNextLocationOffset(child);
            }
        }
    }

    void layoutHorizontal(int left, int top, int right, int bottom) {
        final int paddingTop = getPaddingTop();
        int childTop;
        int childLeft;
        final int height = bottom - top;
        final int childBottom = height - getPaddingBottom();
        final int childSpace = height - paddingTop - getPaddingBottom();
        final int count = getVirtualChildCount();
        final int majorGravity = mGravity & Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK;
        final int minorGravity = mGravity & Gravity.VERTICAL_GRAVITY_MASK;
        final boolean baselineAligned = mBaselineAligned;
        final int[] maxAscent = mMaxAscent;
        final int[] maxDescent = mMaxDescent;

        final int layoutDirection = getLayoutDirection();
        switch (Gravity.getAbsoluteGravity(majorGravity, layoutDirection)) {
            case Gravity.RIGHT:
                childLeft = getPaddingLeft() + right - left - mTotalLength;
                break;
            case Gravity.CENTER_HORIZONTAL:
                childLeft = getPaddingLeft() + (right - left - mTotalLength) / 2;
                break;
            case Gravity.LEFT:
            default:
                childLeft = getPaddingLeft();
                break;
        }

        for (int i = 0; i < count; i++) {
            final View child = getVirtualChildAt(i);
            if (child == null) {
                childLeft += measureNullChild(i);
            } else if (child.getVisibility() != GONE) {
                final int childWidth = child.getMeasuredWidth();
                final int childHeight = child.getMeasuredHeight();
                int childBaseline = -1;
                final LayoutParams lp = (LayoutParams) child.getLayoutParams();
                if (baselineAligned && lp.height != LayoutParams.MATCH_PARENT && maxAscent != null) {
                    childBaseline = child.getBaseline();
                }
                int gravity = lp.gravity;
                if (gravity < 0) gravity = minorGravity;
                switch (gravity & Gravity.VERTICAL_GRAVITY_MASK) {
                    case Gravity.TOP:
                        childTop = paddingTop + lp.topMargin;
                        if (childBaseline != -1) {
                            childTop += maxAscent[INDEX_TOP] - childBaseline;
                        }
                        break;
                    case Gravity.CENTER_VERTICAL:
                        childTop = paddingTop + ((childSpace - childHeight) / 2) + lp.topMargin - lp.bottomMargin;
                        break;
                    case Gravity.BOTTOM:
                        childTop = childBottom - childHeight - lp.bottomMargin;
                        if (childBaseline != -1) {
                            final int descent = child.getMeasuredHeight() - childBaseline;
                            childTop -= maxDescent[INDEX_BOTTOM] - descent;
                        }
                        break;
                    default:
                        childTop = paddingTop;
                        break;
                }
                if (hasDividerBeforeChildAt(i)) childLeft += mDividerWidth;
                childLeft += lp.leftMargin;
                child.layout(childLeft + getLocationOffset(child), childTop,
                        childLeft + getLocationOffset(child) + childWidth, childTop + childHeight);
                childLeft += childWidth + lp.rightMargin + getNextLocationOffset(child);
            }
        }
    }

    /**
     * LinearLayout child parameters.
     *
     * Was an empty stub extending nothing, so `params.weight` and the inherited
     * `width`/`height`/margins all threw NoSuchFieldError. Every programmatic
     * addView() call writes these directly, so they must exist under their AOSP
     * names and the class must extend MarginLayoutParams to inherit the margins.
     */
    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        /** Share of the leftover space along the layout axis. */
        public float weight;

        /** Per-child gravity override, or -1 to inherit the parent's. */
        public int gravity = -1;

        public LayoutParams(Context c, android.util.AttributeSet attrs) {
            super(c, attrs);
            final android.content.res.TypedArray a = c.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.LinearLayout_Layout);
            try {
                weight = a.getFloat(com.android.internal.R.styleable.LinearLayout_Layout_layout_weight, 0);
                gravity = a.getInt(com.android.internal.R.styleable.LinearLayout_Layout_layout_gravity, -1);
            } finally {
                a.recycle();
            }
        }

        public LayoutParams() {
            super(ViewGroup.LayoutParams.MATCH_PARENT,
                  ViewGroup.LayoutParams.WRAP_CONTENT);
        }

        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(int width, int height, float weight) {
            super(width, height);
            this.weight = weight;
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public LayoutParams(ViewGroup.MarginLayoutParams source) {
            super(source);
        }

        public LayoutParams(LayoutParams source) {
            super((ViewGroup.MarginLayoutParams) source);
            this.weight = source.weight;
            this.gravity = source.gravity;
        }
    }

}
