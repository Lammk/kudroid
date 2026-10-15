package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.RelativeLayout — minimal version.
 *
 * Real RelativeLayout resolves a dependency graph of alignment rules, which needs
 * the XML attribute values KuDroid does not parse yet. Until then children are
 * stacked vertically: that keeps them readable and non-overlapping, which is the
 * property that matters, instead of piling every child at the origin.
 */
public class RelativeLayout extends ViewGroup {
    public RelativeLayout(Context context) {
        super(context);
    }

    public RelativeLayout(Context context, android.util.AttributeSet attrs) {
        super(context);
    }

    public RelativeLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        super(context);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int width = 0;
        int height = 0;
        final int availWidth = MeasureSpec.getSize(widthMeasureSpec);
        final int availHeight = MeasureSpec.getSize(heightMeasureSpec);
        final int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == GONE) continue;
            int remaining = availHeight - height;
            if (remaining < 0) remaining = 0;
            child.measure(MeasureSpec.makeMeasureSpec(availWidth, MeasureSpec.AT_MOST),
                          MeasureSpec.makeMeasureSpec(remaining, MeasureSpec.AT_MOST));
            height += child.getMeasuredHeight();
            width = Math.max(width, child.getMeasuredWidth());
        }
        if (MeasureSpec.getMode(widthMeasureSpec) != MeasureSpec.UNSPECIFIED) {
            width = Math.max(width, availWidth);
        }
        if (MeasureSpec.getMode(heightMeasureSpec) != MeasureSpec.UNSPECIFIED) {
            height = Math.max(height, availHeight);
        }
        setMeasuredDimension(width, height);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int curTop = t;
        final int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() == GONE) continue;
            int ch = child.getMeasuredHeight();
            child.layout(l, curTop, r, curTop + ch);
            curTop += ch;
        }
    }

    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public void addRule(int verb) {
        }

        public void addRule(int verb, int anchor) {
        }
    }
}
