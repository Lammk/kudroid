package android.view;

import android.content.Context;

/**
 * minimal android.view.viewgroup implementation.
 *
 * a view contains other views. for kudroid minimal framework, this
 * Provides basic child management.
 */
public class ViewGroup extends View implements ViewParent {
    // protected so subclasses in this package tree can iterate children directly.
    protected View[] mChildren;
    protected int mChildCount = 0;

    public ViewGroup(Context context) {
        this(context, null);
    }

    public ViewGroup(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public ViewGroup(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public ViewGroup(Context context, android.util.AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        mChildren = new View[16];
    }

    /**
     * add a child view.
     */
    public void addView(View child) {
        if (mChildCount >= mChildren.length) {
            View[] bigger = new View[mChildren.length * 2];
            System.arraycopy(mChildren, 0, bigger, 0, mChildren.length);
            mChildren = bigger;
        }
        mChildren[mChildCount++] = child;
        child.setParent(this);
    }

    /**
     * add a child view with layout parameters.
     */
    public void addView(View child, LayoutParams params) {
        addView(child);
    }

    /**
     * delete a child view.
     */
    public void removeView(View view) {
        for (int i = 0; i < mChildCount; i++) {
            if (mChildren[i] == view) {
                System.arraycopy(mChildren, i + 1, mChildren, i, mChildCount - i - 1);
                mChildCount--;
                view.setParent(null);
                return;
            }
        }
    }

    /**
     * delete all child views.
     */
    public void removeAllViews() {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].setParent(null);
        }
        mChildCount = 0;
    }

    /**
     * returns the number of child views.
     */
    public int getChildCount() {
        return mChildCount;
    }

    /**
     * returns the child view at the given index.
     */
    public View getChildAt(int index) {
        if (index < 0 || index >= mChildCount) return null;
        return mChildren[index];
    }

    /**
     * find a child view by id.
     */
    public View findViewById(int id) {
        if (getId() == id) return this;
        for (int i = 0; i < mChildCount; i++) {
            View child = mChildren[i];
            if (child == null) continue;
            if (child.getId() == id) return child;
            View found = child.findViewById(id);
            if (found != null) return found;
        }
        return null;
    }

    /**
     * Measure every child, then report the largest child size. Subclasses that stack
     * or grid their children override this; the default is the FrameLayout rule of
     * overlapping everything at the origin.
     */
    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int maxWidth = 0;
        int maxHeight = 0;
        for (int i = 0; i < mChildCount; i++) {
            View child = mChildren[i];
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

    /**
     * Position children. Not abstract: View now supplies a no-op default, and forcing
     * every container to reimplement this was why stub layouts could not extend
     * ViewGroup at all. The default overlaps children at the group's origin.
     */
    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        for (int i = 0; i < mChildCount; i++) {
            View child = mChildren[i];
            if (child == null || child.getVisibility() == GONE) continue;
            child.layout(l, t, l + child.getMeasuredWidth(), t + child.getMeasuredHeight());
        }
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (!isShown() || event == null) return false;
        float x = event.getX();
        float y = event.getY();
        for (int i = mChildCount - 1; i >= 0; i--) {
            View child = mChildren[i];
            if (child != null && child.isShown()) {
                if (x >= child.getLeft() && x <= child.getRight() &&
                    y >= child.getTop() && y <= child.getBottom()) {
                    if (child.dispatchTouchEvent(event)) {
                        return true;
                    }
                }
            }
        }
        return onTouchEvent(event);
    }

    @Override
    public void draw(android.graphics.Canvas canvas) {
        super.draw(canvas);
        dispatchDraw(canvas);
    }

    protected void dispatchDraw(android.graphics.Canvas canvas) {
        for (int i = 0; i < mChildCount; i++) {
            View child = mChildren[i];
            if (child != null && child.isShown()) {
                child.draw(canvas);
            }
        }
    }

    /**
     * layout parameters for a view.
     */
    public static class LayoutParams {
        /**
         * Sizing sentinels.
         *
         * These were missing, so any app passing MATCH_PARENT/WRAP_CONTENT — which is
         * how nearly every programmatic addView() call sizes a child — hit
         * NoSuchFieldError. The values are the AOSP ones and app code compares
         * against them directly, so they cannot be renumbered.
         */
        public static final int FILL_PARENT = -1;
        public static final int MATCH_PARENT = -1;
        public static final int WRAP_CONTENT = -2;

        public int width;
        public int height;

        public LayoutParams() {
            this(WRAP_CONTENT, WRAP_CONTENT);
        }

        public LayoutParams(int width, int height) {
            this.width = width;
            this.height = height;
        }

        public LayoutParams(LayoutParams source) {
            this.width = source != null ? source.width : WRAP_CONTENT;
            this.height = source != null ? source.height : WRAP_CONTENT;
        }
    }

    /**
     * margin layout parameters.
     */
    public static class MarginLayoutParams extends LayoutParams {
        public int leftMargin;
        public int topMargin;
        public int rightMargin;
        public int bottomMargin;

        public MarginLayoutParams() {
            super();
        }

        public MarginLayoutParams(int width, int height) {
            super(width, height);
        }

        public MarginLayoutParams(LayoutParams source) {
            super(source);
        }

        public void setMargins(int left, int top, int right, int bottom) {
            leftMargin = left;
            topMargin = top;
            rightMargin = right;
            bottomMargin = bottom;
        }

        public void setMarginStart(int start) { leftMargin = start; }
        public int getMarginStart() { return leftMargin; }
        public void setMarginEnd(int end) { rightMargin = end; }
        public int getMarginEnd() { return rightMargin; }
    }

    public interface OnHierarchyChangeListener {
    }

    // ── ViewParent ───────────────────────────────────────────────────────────────────
    //
    // A child talks upwards through this interface rather than through ViewGroup, which is
    // why View.getParent() has to return ViewParent: apps assign the result to a
    // ViewParent variable, and a getParent() declared as returning ViewGroup does not
    // satisfy that reference — it fails as a missing method on a method that looks present.
    //
    // requestLayout() and isLayoutRequested() are inherited from View, which already
    // implements them for the top-down layout model; the rest are below.

    /**
     * A child's content changed.
     *
     * Redraw is driven from the top by Activity.renderViewHierarchy(), so the rectangle is
     * not used: there is no dirty-region tracking to feed it into. Repainting the whole
     * hierarchy is what already happens, and pretending to honour the rect would be a lie
     * a caller could act on.
     */
    @Override
    public void invalidateChild(View child, android.graphics.Rect r) {
        invalidate();
    }

    /**
     * A child asks ancestors not to intercept the rest of this gesture.
     *
     * Empty because nothing here intercepts: touch events reach the child directly. Present
     * because scrolling containers call it unconditionally on the way down, and a missing
     * method there is a crash rather than a layout quirk.
     */
    @Override
    public void requestDisallowInterceptTouchEvent(boolean disallowIntercept) {
    }

    @Override
    public void requestChildFocus(View child, View focused) {
        // Focus is tracked on the View itself; a container has nothing to record. Passed up
        // so an ancestor that does care still hears about it.
        ViewParent parent = getParent();
        if (parent != null) parent.requestChildFocus(this, focused);
    }

    @Override
    public void clearChildFocus(View child) {
        ViewParent parent = getParent();
        if (parent != null) parent.clearChildFocus(this);
    }

    /**
     * Focus navigation, which KuDroid does not implement.
     *
     * Returns null — "nothing to move to" — rather than an arbitrary view. A wrong answer
     * would move focus somewhere the user did not ask for, which is worse than focus not
     * moving at all.
     */
    @Override
    public View focusSearch(View v, int direction) {
        return null;
    }
}
