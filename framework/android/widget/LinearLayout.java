package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/**
 * minimal android.widget.linearlayout implementation.
 *
 * arrange child views in a single row or column. for framework
 * minimal of kudroid, provides basic layout.
 */
public class LinearLayout extends ViewGroup {
    /** horizontal direction. */
    public static final int HORIZONTAL = 0;
    /** vertical direction. */
    public static final int VERTICAL = 1;

    private int mOrientation = VERTICAL;
    private int mGravity = 0;

    public LinearLayout(Context context) {
        super(context);
    }

    public LinearLayout(Context context, android.util.AttributeSet attrs) {
        super(context);
    }

    public LinearLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        super(context);
    }

    /**
     * direction setting.
     */
    public void setOrientation(int orientation) {
        mOrientation = orientation;
    }

    /**
     * returns direction.
     */
    public int getOrientation() {
        return mOrientation;
    }

    /**
     * set gravity.
     */
    public void setGravity(int gravity) {
        mGravity = gravity;
    }

    /**
     * returns gravity.
     */
    public int getGravity() {
        return mGravity;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int totalWidth = 0;
        int totalHeight = 0;
        int count = getChildCount();

        // Children are offered the space left over along the stacking axis and the
        // parent's full extent across it. Their reply comes back via
        // getMeasuredWidth/Height, not getWidth/Height — the latter are only valid
        // after layout() has positioned them, so reading those here (as this used to)
        // always saw zero.
        final int availWidth = MeasureSpec.getSize(widthMeasureSpec);
        final int availHeight = MeasureSpec.getSize(heightMeasureSpec);

        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == GONE) continue;

            int childWidthSpec;
            int childHeightSpec;
            if (mOrientation == VERTICAL) {
                childWidthSpec = MeasureSpec.makeMeasureSpec(availWidth, MeasureSpec.AT_MOST);
                int remaining = availHeight - totalHeight;
                if (remaining < 0) remaining = 0;
                childHeightSpec = MeasureSpec.makeMeasureSpec(remaining, MeasureSpec.AT_MOST);
            } else {
                int remaining = availWidth - totalWidth;
                if (remaining < 0) remaining = 0;
                childWidthSpec = MeasureSpec.makeMeasureSpec(remaining, MeasureSpec.AT_MOST);
                childHeightSpec = MeasureSpec.makeMeasureSpec(availHeight, MeasureSpec.AT_MOST);
            }
            child.measure(childWidthSpec, childHeightSpec);

            if (mOrientation == VERTICAL) {
                totalHeight += child.getMeasuredHeight();
                totalWidth = Math.max(totalWidth, child.getMeasuredWidth());
            } else {
                totalWidth += child.getMeasuredWidth();
                totalHeight = Math.max(totalHeight, child.getMeasuredHeight());
            }
        }

        // A background is only visible where the layout actually extends, so fill the
        // space the parent gave us rather than shrink-wrapping the children.
        if (MeasureSpec.getMode(widthMeasureSpec) != MeasureSpec.UNSPECIFIED) {
            totalWidth = Math.max(totalWidth, availWidth);
        }
        if (MeasureSpec.getMode(heightMeasureSpec) != MeasureSpec.UNSPECIFIED) {
            totalHeight = Math.max(totalHeight, availHeight);
        }
        setMeasuredDimension(totalWidth, totalHeight);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int count = getChildCount();
        int curTop = t;
        int curLeft = l;
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == GONE) continue;

            // Positions come from the MEASURED size. Using getWidth()/getHeight()
            // here would read the bounds we are about to overwrite, which are still
            // zero on the first pass.
            int childWidth = child.getMeasuredWidth();
            int childHeight = child.getMeasuredHeight();
            if (mOrientation == VERTICAL) {
                // Stretch across the layout so a child's background spans the row.
                child.layout(curLeft, curTop, r, curTop + childHeight);
                curTop += childHeight;
            } else {
                child.layout(curLeft, curTop, curLeft + childWidth, b);
                curLeft += childWidth;
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
    }

}
