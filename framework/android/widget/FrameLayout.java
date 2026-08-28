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
    public FrameLayout(Context context) {
        super(context);
    }

    public FrameLayout(Context context, android.util.AttributeSet attrs) {
        super(context);
    }

    public FrameLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        super(context);
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
            child.layout(l, t, l + cw, t + ch);
        }
    }

    public static class LayoutParams extends ViewGroup.LayoutParams {
        public int gravity = -1;

        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(int width, int height, int gravity) {
            super(width, height);
            this.gravity = gravity;
        }
    }
}
