package android.widget;

import android.content.Context;
import android.view.View;

/**
 * android.widget.ScrollView — a FrameLayout that can be scrolled vertically.
 *
 * Scrolling itself is a translation applied at draw time; the child is measured
 * with an unbounded height so it can report its true size rather than being
 * clipped to the viewport.
 */
public class ScrollView extends FrameLayout {
    private int mScrollY;

    public ScrollView(Context context) {
        super(context);
    }

    public ScrollView(Context context, android.util.AttributeSet attrs) {
        super(context);
    }

    public ScrollView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        super(context);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        View child = getChildCount() > 0 ? getChildAt(0) : null;
        if (child != null && child.getVisibility() != GONE) {
            // UNSPECIFIED height: content taller than the screen is the normal case.
            child.measure(widthMeasureSpec,
                          MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
        }
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec),
                             MeasureSpec.getSize(heightMeasureSpec));
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        View child = getChildCount() > 0 ? getChildAt(0) : null;
        if (child == null || child.getVisibility() == GONE) return;
        final int top = t - mScrollY;
        child.layout(l, top, r, top + child.getMeasuredHeight());
    }

    public void scrollTo(int x, int y) {
        mScrollY = Math.max(0, y);
        requestLayout();
    }

    public void scrollBy(int x, int y) {
        scrollTo(0, mScrollY + y);
    }

    public int getScrollY() {
        return mScrollY;
    }

    public void fullScroll(int direction) {
    }

    public void smoothScrollTo(int x, int y) {
        scrollTo(x, y);
    }

    public void smoothScrollBy(int dx, int dy) {
        scrollBy(dx, dy);
    }
}
