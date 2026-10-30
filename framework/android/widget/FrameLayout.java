package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.FrameLayout — children stack on top of each other at the origin.
 *
 * This is the layout an Activity's decor view uses, so it has to be a real
 * ViewGroup: as an empty stub it could not hold children at all, and anything
 * inflating a layout got a blank screen.
 */
public class FrameLayout extends ViewGroup {
    private boolean mMeasureAllChildren;

    public FrameLayout(Context context) {
        this(context, null);
    }

    public FrameLayout(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public FrameLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public FrameLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (context != null && (attrs != null || defStyleAttr != 0 || defStyleRes != 0)) {
            final android.content.res.TypedArray a = context.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.FrameLayout, defStyleAttr, defStyleRes);
            try {
                mMeasureAllChildren = a.getBoolean(
                        com.android.internal.R.styleable.FrameLayout_measureAllChildren, false);
            } finally {
                a.recycle();
            }
        }
    }

    public boolean getMeasureAllChildren() { return mMeasureAllChildren; }
    public void setMeasureAllChildren(boolean measureAll) { mMeasureAllChildren = measureAll; }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
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

    private final java.util.ArrayList<View> mMatchParentChildren = new java.util.ArrayList<View>(1);

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        final int count = getChildCount();
        final boolean measureMatchParentChildren =
                MeasureSpec.getMode(widthMeasureSpec) != MeasureSpec.EXACTLY
                        || MeasureSpec.getMode(heightMeasureSpec) != MeasureSpec.EXACTLY;
        mMatchParentChildren.clear();

        int maxHeight = 0;
        int maxWidth = 0;
        int childState = 0;
        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            if (child == null || (!mMeasureAllChildren && child.getVisibility() == GONE)) continue;
            measureChildWithMargins(child, widthMeasureSpec, 0, heightMeasureSpec, 0);
            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            maxWidth = Math.max(maxWidth, child.getMeasuredWidth() + lp.leftMargin + lp.rightMargin);
            maxHeight = Math.max(maxHeight, child.getMeasuredHeight() + lp.topMargin + lp.bottomMargin);
            childState = combineMeasuredStates(childState, child.getMeasuredState());
            if (measureMatchParentChildren
                    && (lp.width == LayoutParams.MATCH_PARENT || lp.height == LayoutParams.MATCH_PARENT)) {
                mMatchParentChildren.add(child);
            }
        }

        maxWidth += getPaddingLeft() + getPaddingRight();
        maxHeight += getPaddingTop() + getPaddingBottom();
        maxHeight = Math.max(maxHeight, getSuggestedMinimumHeight());
        maxWidth = Math.max(maxWidth, getSuggestedMinimumWidth());
        final android.graphics.drawable.Drawable foreground = getForeground();
        if (foreground != null) {
            maxHeight = Math.max(maxHeight, foreground.getMinimumHeight());
            maxWidth = Math.max(maxWidth, foreground.getMinimumWidth());
        }
        setMeasuredDimension(resolveSizeAndState(maxWidth, widthMeasureSpec, childState),
                resolveSizeAndState(maxHeight, heightMeasureSpec, childState << MEASURED_HEIGHT_STATE_SHIFT));

        // Children that asked to match a parent whose size was only just decided are measured again.
        if (mMatchParentChildren.size() > 1) {
            for (int i = 0; i < mMatchParentChildren.size(); i++) {
                final View child = mMatchParentChildren.get(i);
                final ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
                final int childWidthMeasureSpec;
                if (lp.width == LayoutParams.MATCH_PARENT) {
                    final int width = Math.max(0, getMeasuredWidth() - getPaddingLeft() - getPaddingRight()
                            - lp.leftMargin - lp.rightMargin);
                    childWidthMeasureSpec = MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY);
                } else {
                    childWidthMeasureSpec = getChildMeasureSpec(widthMeasureSpec,
                            getPaddingLeft() + getPaddingRight() + lp.leftMargin + lp.rightMargin, lp.width);
                }
                final int childHeightMeasureSpec;
                if (lp.height == LayoutParams.MATCH_PARENT) {
                    final int height = Math.max(0, getMeasuredHeight() - getPaddingTop() - getPaddingBottom()
                            - lp.topMargin - lp.bottomMargin);
                    childHeightMeasureSpec = MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY);
                } else {
                    childHeightMeasureSpec = getChildMeasureSpec(heightMeasureSpec,
                            getPaddingTop() + getPaddingBottom() + lp.topMargin + lp.bottomMargin, lp.height);
                }
                child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
            }
        }
    }

    private static final int DEFAULT_CHILD_GRAVITY = android.view.Gravity.TOP | android.view.Gravity.START;

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        layoutChildren(left, top, right, bottom, false);
    }

    void layoutChildren(int left, int top, int right, int bottom, boolean forceLeftGravity) {
        final int count = getChildCount();
        final int parentLeft = getPaddingLeft();
        final int parentRight = right - left - getPaddingRight();
        final int parentTop = getPaddingTop();
        final int parentBottom = bottom - top - getPaddingBottom();

        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            if (child == null || child.getVisibility() == GONE) continue;
            final LayoutParams lp = (LayoutParams) child.getLayoutParams();
            final int width = child.getMeasuredWidth();
            final int height = child.getMeasuredHeight();

            int gravity = lp.gravity;
            if (gravity == -1) gravity = DEFAULT_CHILD_GRAVITY;
            final int absoluteGravity = android.view.Gravity.getAbsoluteGravity(gravity, getLayoutDirection());
            final int verticalGravity = gravity & android.view.Gravity.VERTICAL_GRAVITY_MASK;

            final int childLeft;
            switch (absoluteGravity & android.view.Gravity.HORIZONTAL_GRAVITY_MASK) {
                case android.view.Gravity.CENTER_HORIZONTAL:
                    childLeft = parentLeft + (parentRight - parentLeft - width) / 2
                            + lp.leftMargin - lp.rightMargin;
                    break;
                case android.view.Gravity.RIGHT:
                    if (!forceLeftGravity) {
                        childLeft = parentRight - width - lp.rightMargin;
                        break;
                    }
                    // fall through
                case android.view.Gravity.LEFT:
                default:
                    childLeft = parentLeft + lp.leftMargin;
            }

            final int childTop;
            switch (verticalGravity) {
                case android.view.Gravity.CENTER_VERTICAL:
                    childTop = parentTop + (parentBottom - parentTop - height) / 2
                            + lp.topMargin - lp.bottomMargin;
                    break;
                case android.view.Gravity.BOTTOM:
                    childTop = parentBottom - height - lp.bottomMargin;
                    break;
                case android.view.Gravity.TOP:
                default:
                    childTop = parentTop + lp.topMargin;
            }
            child.layout(childLeft, childTop, childLeft + width, childTop + height);
        }
    }

    /** FrameLayout child parameters; extends MarginLayoutParams so margins exist. */
    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        public static final int UNSPECIFIED_GRAVITY = -1;

        public int gravity = UNSPECIFIED_GRAVITY;

        public LayoutParams(Context c, android.util.AttributeSet attrs) {
            super(c, attrs);
            final android.content.res.TypedArray a = c.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.FrameLayout_Layout);
            try {
                gravity = a.getInt(com.android.internal.R.styleable.FrameLayout_Layout_layout_gravity,
                        UNSPECIFIED_GRAVITY);
            } finally {
                a.recycle();
            }
        }

        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(int width, int height, int gravity) {
            super(width, height);
            this.gravity = gravity;
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public LayoutParams(ViewGroup.MarginLayoutParams source) {
            super(source);
        }

        public LayoutParams(LayoutParams source) {
            super((ViewGroup.MarginLayoutParams) source);
            this.gravity = source.gravity;
        }
    }
}
