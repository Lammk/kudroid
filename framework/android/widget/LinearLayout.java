package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/**
 * triển khai android.widget.linearlayout tối thiểu.
 *
 * sắp xếp các view con trong một hàng hoặc cột duy nhất. đối với khuôn khổ
 * tối thiểu của kudroid, cung cấp bố cục cơ bản.
 */
public class LinearLayout extends ViewGroup {
    /** hướng ngang. */
    public static final int HORIZONTAL = 0;
    /** hướng dọc. */
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
     * thiết lập hướng.
     */
    public void setOrientation(int orientation) {
        mOrientation = orientation;
    }

    /**
     * trả về hướng.
     */
    public int getOrientation() {
        return mOrientation;
    }

    /**
     * thiết lập trọng lực (gravity).
     */
    public void setGravity(int gravity) {
        mGravity = gravity;
    }

    /**
     * trả về trọng lực (gravity).
     */
    public int getGravity() {
        return mGravity;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int totalWidth = 0;
        int totalHeight = 0;
        int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            child.measure(widthMeasureSpec, heightMeasureSpec);
            if (mOrientation == VERTICAL) {
                totalHeight += child.getHeight();
                totalWidth = Math.max(totalWidth, child.getWidth());
            } else {
                totalWidth += child.getWidth();
                totalHeight = Math.max(totalHeight, child.getHeight());
            }
        }
        layout(0, 0, totalWidth, totalHeight);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int count = getChildCount();
        int curTop = t;
        int curLeft = l;
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            int childWidth = child.getWidth();
            int childHeight = child.getHeight();
            if (mOrientation == VERTICAL) {
                child.layout(curLeft, curTop, curLeft + childWidth, curTop + childHeight);
                curTop += childHeight;
            } else {
                child.layout(curLeft, curTop, curLeft + childWidth, curTop + childHeight);
                curLeft += childWidth;
            }
        }
    }
}
