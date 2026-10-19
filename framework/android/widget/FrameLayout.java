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

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        // Children each get the full frame; the frame itself takes the space offered.
        int maxWidth = 0;
        int maxHeight = 0;
        final int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == GONE) continue;
            child.measure(widthMeasureSpec, heightMeasureSpec);
            maxWidth = Math.max(maxWidth, child.getMeasuredWidth());
            maxHeight = Math.max(maxHeight, child.getMeasuredHeight());
        }
        if (MeasureSpec.getMode(widthMeasureSpec) != MeasureSpec.UNSPECIFIED) {
            maxWidth = Math.max(maxWidth, MeasureSpec.getSize(widthMeasureSpec));
        }
        if (MeasureSpec.getMode(heightMeasureSpec) != MeasureSpec.UNSPECIFIED) {
            maxHeight = Math.max(maxHeight, MeasureSpec.getSize(heightMeasureSpec));
        }
        setMeasuredDimension(maxWidth, maxHeight);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        final int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == GONE) continue;
            // Each child fills the frame unless it measured smaller.
            int cw = child.getMeasuredWidth();
            int ch = child.getMeasuredHeight();
            if (cw <= 0) cw = r - l;
            if (ch <= 0) ch = b - t;
            child.layout(0, 0, cw, ch);
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
