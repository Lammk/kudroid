package android.view;

import android.content.Context;

/**
 * minimal android.view.viewgroup implementation.
 *
 * a view contains other views. for kudroid minimal framework, this
 * Provides basic child management.
 */
public class ViewGroup extends View implements ViewParent {
    @Override
    protected void dispatchCancelPendingInputEvents() {
        super.dispatchCancelPendingInputEvents();
        for (int i = 0; i < getChildCount(); i++) getChildAt(i).dispatchCancelPendingInputEvents();
    }
    // protected so subclasses in this package tree can iterate children directly.
    protected View[] mChildren;
    protected int mChildCount = 0;
    // Child that captured the current gesture (AOSP mFirstTouchTarget). Fixed
    // at DOWN, cleared at UP/CANCEL.
    private View mTouchTarget;

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
        if (context != null && (attrs != null || defStyleAttr != 0 || defStyleRes != 0)) {
            final android.content.res.TypedArray a = context.obtainStyledAttributes(
                    attrs, com.android.internal.R.styleable.ViewGroup, defStyleAttr, defStyleRes);
            try {
                final int n = a.getIndexCount();
                for (int i = 0; i < n; i++) {
                    final int attr = a.getIndex(i);
                    if (attr == com.android.internal.R.styleable.ViewGroup_clipChildren) {
                        mClipChildren = a.getBoolean(attr, true);
                    } else if (attr == com.android.internal.R.styleable.ViewGroup_clipToPadding) {
                        mClipToPadding = a.getBoolean(attr, true);
                    } else if (attr == com.android.internal.R.styleable.ViewGroup_descendantFocusability) {
                        mDescendantFocusability = a.getInt(attr, FOCUS_BEFORE_DESCENDANTS);
                    }
                }
            } finally {
                a.recycle();
            }
        }
    }

    public static final int FOCUS_BEFORE_DESCENDANTS = 0x20000;
    public static final int FOCUS_AFTER_DESCENDANTS = 0x40000;
    public static final int FOCUS_BLOCK_DESCENDANTS = 0x60000;

    private boolean mClipChildren = true;
    private boolean mClipToPadding = true;
    private int mDescendantFocusability = FOCUS_BEFORE_DESCENDANTS;
    private OnHierarchyChangeListener mOnHierarchyChangeListener;

    public boolean getClipChildren() { return mClipChildren; }
    public void setClipChildren(boolean clipChildren) { mClipChildren = clipChildren; }
    public boolean getClipToPadding() { return mClipToPadding; }
    public void setClipToPadding(boolean clipToPadding) { mClipToPadding = clipToPadding; }
    public int getDescendantFocusability() { return mDescendantFocusability; }
    public void setDescendantFocusability(int focusability) { mDescendantFocusability = focusability; }

    public void setOnHierarchyChangeListener(OnHierarchyChangeListener listener) {
        mOnHierarchyChangeListener = listener;
    }

    /** True when this group takes over the gesture instead of its children. */
    public boolean onInterceptTouchEvent(MotionEvent ev) {
        return false;
    }

    // LayoutParams generation.

    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
    }

    public LayoutParams generateLayoutParams(android.util.AttributeSet attrs) {
        return new LayoutParams(getContext(), attrs);
    }

    protected LayoutParams generateLayoutParams(LayoutParams p) {
        return p;
    }

    protected boolean checkLayoutParams(LayoutParams p) {
        return p != null;
    }

    // Measuring children.

    protected void measureChildren(int widthMeasureSpec, int heightMeasureSpec) {
        for (int i = 0; i < mChildCount; i++) {
            final View child = mChildren[i];
            if (child != null && child.getVisibility() != GONE) {
                measureChild(child, widthMeasureSpec, heightMeasureSpec);
            }
        }
    }

    protected void measureChild(View child, int parentWidthMeasureSpec, int parentHeightMeasureSpec) {
        final LayoutParams lp = child.getLayoutParams();
        final int childWidthMeasureSpec = getChildMeasureSpec(parentWidthMeasureSpec,
                mPaddingLeft + mPaddingRight, lp.width);
        final int childHeightMeasureSpec = getChildMeasureSpec(parentHeightMeasureSpec,
                mPaddingTop + mPaddingBottom, lp.height);
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    protected void measureChildWithMargins(View child, int parentWidthMeasureSpec, int widthUsed,
            int parentHeightMeasureSpec, int heightUsed) {
        final MarginLayoutParams lp = (MarginLayoutParams) child.getLayoutParams();
        final int childWidthMeasureSpec = getChildMeasureSpec(parentWidthMeasureSpec,
                mPaddingLeft + mPaddingRight + lp.leftMargin + lp.rightMargin + widthUsed, lp.width);
        final int childHeightMeasureSpec = getChildMeasureSpec(parentHeightMeasureSpec,
                mPaddingTop + mPaddingBottom + lp.topMargin + lp.bottomMargin + heightUsed, lp.height);
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    /** The spec a child gets from its parent's spec, padding and requested dimension. */
    public static int getChildMeasureSpec(int spec, int padding, int childDimension) {
        final int specMode = MeasureSpec.getMode(spec);
        final int specSize = MeasureSpec.getSize(spec);
        final int size = Math.max(0, specSize - padding);
        int resultSize = 0;
        int resultMode = 0;
        switch (specMode) {
            case MeasureSpec.EXACTLY:
                if (childDimension >= 0) {
                    resultSize = childDimension;
                    resultMode = MeasureSpec.EXACTLY;
                } else if (childDimension == LayoutParams.MATCH_PARENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.EXACTLY;
                } else if (childDimension == LayoutParams.WRAP_CONTENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.AT_MOST;
                }
                break;
            case MeasureSpec.AT_MOST:
                if (childDimension >= 0) {
                    resultSize = childDimension;
                    resultMode = MeasureSpec.EXACTLY;
                } else if (childDimension == LayoutParams.MATCH_PARENT
                        || childDimension == LayoutParams.WRAP_CONTENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.AT_MOST;
                }
                break;
            case MeasureSpec.UNSPECIFIED:
                if (childDimension >= 0) {
                    resultSize = childDimension;
                    resultMode = MeasureSpec.EXACTLY;
                } else if (childDimension == LayoutParams.MATCH_PARENT
                        || childDimension == LayoutParams.WRAP_CONTENT) {
                    resultSize = size;
                    resultMode = MeasureSpec.UNSPECIFIED;
                }
                break;
        }
        return MeasureSpec.makeMeasureSpec(resultSize, resultMode);
    }

    /** Add a child at the end, with its own LayoutParams or this group's defaults. */
    public void addView(View child) {
        addView(child, -1);
    }

    public void addView(View child, int index) {
        if (child == null) {
            throw new IllegalArgumentException("Cannot add a null child view to a ViewGroup");
        }
        LayoutParams params = child.getLayoutParams();
        if (params == null) {
            params = generateDefaultLayoutParams();
            if (params == null) {
                throw new IllegalArgumentException("generateDefaultLayoutParams() cannot return null");
            }
        }
        addView(child, index, params);
    }

    public void addView(View child, int width, int height) {
        final LayoutParams params = generateDefaultLayoutParams();
        params.width = width;
        params.height = height;
        addView(child, -1, params);
    }

    public void addView(View child, LayoutParams params) {
        addView(child, -1, params);
    }

    public void addView(View child, int index, LayoutParams params) {
        if (child == null) {
            throw new IllegalArgumentException("Cannot add a null child view to a ViewGroup");
        }
        addViewInner(child, index, params);
        requestLayout();
        invalidate();
    }

    private void addViewInner(View child, int index, LayoutParams params) {
        if (child.getParent() != null) {
            throw new IllegalStateException("The specified child already has a parent. "
                    + "You must call removeView() on the child's parent first.");
        }
        if (!checkLayoutParams(params)) params = generateLayoutParams(params);
        child.setLayoutParamsNoRequest(params);
        if (index < 0 || index > mChildCount) index = mChildCount;
        if (mChildCount >= mChildren.length) {
            View[] bigger = new View[mChildren.length * 2];
            System.arraycopy(mChildren, 0, bigger, 0, mChildren.length);
            mChildren = bigger;
        }
        System.arraycopy(mChildren, index, mChildren, index + 1, mChildCount - index);
        mChildren[index] = child;
        mChildCount++;
        child.setParent(this);
        onViewAdded(child);
    }

    public void onViewAdded(View child) {
        if (mOnHierarchyChangeListener != null) {
            mOnHierarchyChangeListener.onChildViewAdded(this, child);
        }
    }

    public void onViewRemoved(View child) {
        if (mOnHierarchyChangeListener != null) {
            mOnHierarchyChangeListener.onChildViewRemoved(this, child);
        }
    }

    private void removeViewInternal(int index) {
        final View view = mChildren[index];
        System.arraycopy(mChildren, index + 1, mChildren, index, mChildCount - index - 1);
        mChildCount--;
        mChildren[mChildCount] = null;
        view.setParent(null);
        onViewRemoved(view);
    }

    /** delete a child view. */
    public void removeView(View view) {
        final int index = indexOfChild(view);
        if (index >= 0) {
            removeViewInternal(index);
            requestLayout();
            invalidate();
        }
    }

    public void removeViewInLayout(View view) {
        final int index = indexOfChild(view);
        if (index >= 0) removeViewInternal(index);
    }

    public void removeViews(int start, int count) {
        if (count <= 0) return;
        final int end = Math.min(mChildCount, start + count);
        if (start < 0 || start >= mChildCount) {
            throw new IndexOutOfBoundsException();
        }
        for (int i = end - 1; i >= start; i--) removeViewInternal(i);
        requestLayout();
        invalidate();
    }

    public int indexOfChild(View child) {
        for (int i = 0; i < mChildCount; i++) {
            if (mChildren[i] == child) return i;
        }
        return -1;
    }

    public void removeViewAt(int index) {
        if (index < 0 || index >= mChildCount) {
            throw new IndexOutOfBoundsException("index=" + index + " count=" + mChildCount);
        }
        removeViewInternal(index);
        requestLayout();
        invalidate();
    }

    public void bringChildToFront(View child) {
        int index = indexOfChild(child);
        if (index >= 0 && index != mChildCount - 1) {
            System.arraycopy(mChildren, index + 1, mChildren, index, mChildCount - index - 1);
            mChildren[mChildCount - 1] = child;
            requestLayout();
            invalidate();
        }
    }

    /** delete all child views. */
    public void removeAllViews() {
        for (int i = mChildCount - 1; i >= 0; i--) removeViewInternal(i);
        requestLayout();
        invalidate();
    }

    public void removeAllViewsInLayout() {
        for (int i = mChildCount - 1; i >= 0; i--) removeViewInternal(i);
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
        if (id == NO_ID) return null;
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

    public View findViewWithTag(Object tag) {
        if (tag == null) return null;
        if (tag.equals(getTag())) return this;
        for (int i = 0; i < mChildCount; i++) {
            final View found = mChildren[i].findViewWithTag(tag);
            if (found != null) return found;
        }
        return null;
    }

    public View getFocusedChild() {
        for (int i = 0; i < mChildCount; i++) {
            if (mChildren[i].hasFocus()) return mChildren[i];
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
            child.layout(0, 0, child.getMeasuredWidth(), child.getMeasuredHeight());
        }
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (!isShown() || event == null) return false;
        final int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            if (mTouchTarget != null) {
                MotionEvent cancel = MotionEvent.obtain(event);
                cancel.setAction(MotionEvent.ACTION_CANCEL);
                try { dispatchToChild(mTouchTarget, cancel); }
                finally { cancel.recycle(); }
            }
            mTouchTarget = null;
            final float x = event.getX() + mScrollX;
            final float y = event.getY() + mScrollY;
            for (int i = mChildCount - 1; i >= 0; --i) {
                View child = mChildren[i];
                if (child == null || !child.isShown() || x < child.getLeft() ||
                    x >= child.getRight() || y < child.getTop() || y >= child.getBottom()) continue;
                if (dispatchToChild(child, event)) {
                    mTouchTarget = child;
                    return true;
                }
            }
        } else if (mTouchTarget != null) {
            // An unsplit gesture keeps every pointer with its original child.
            View child = mTouchTarget;
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                mTouchTarget = null;
            }
            return dispatchToChild(child, event);
        }
        return super.dispatchTouchEvent(event);
    }

    private boolean dispatchToChild(View child, MotionEvent event) {
        MotionEvent local = MotionEvent.obtain(event);
        local.offsetLocation(mScrollX - child.getLeft(), mScrollY - child.getTop());
        try {
            return child.dispatchTouchEvent(local);
        } finally {
            local.recycle();
        }
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
        /** Sizing sentinels; the values are Android's and apps compare against them. */
        public static final int FILL_PARENT = -1;
        public static final int MATCH_PARENT = -1;
        public static final int WRAP_CONTENT = -2;

        public int width;
        public int height;

        /** Read layout_width and layout_height from XML. */
        public LayoutParams(Context c, android.util.AttributeSet attrs) {
            final android.content.res.TypedArray a = c.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.ViewGroup_Layout);
            try {
                setBaseAttributes(a, com.android.internal.R.styleable.ViewGroup_Layout_layout_width,
                        com.android.internal.R.styleable.ViewGroup_Layout_layout_height);
            } finally {
                a.recycle();
            }
        }

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

        protected void setBaseAttributes(android.content.res.TypedArray a, int widthAttr, int heightAttr) {
            width = a.getLayoutDimension(widthAttr, "layout_width");
            height = a.getLayoutDimension(heightAttr, "layout_height");
        }

        public void resolveLayoutDirection(int layoutDirection) {
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

        public MarginLayoutParams(Context c, android.util.AttributeSet attrs) {
            super();
            final android.content.res.TypedArray a = c.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.ViewGroup_MarginLayout);
            try {
                setBaseAttributes(a,
                        com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_width,
                        com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_height);
                final int margin = a.getDimensionPixelSize(
                        com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_margin, -1);
                if (margin >= 0) {
                    leftMargin = topMargin = rightMargin = bottomMargin = margin;
                } else {
                    final int start = a.getDimensionPixelSize(
                            com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_marginStart, -1);
                    final int end = a.getDimensionPixelSize(
                            com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_marginEnd, -1);
                    final int left = a.getDimensionPixelSize(
                            com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_marginLeft, -1);
                    final int right = a.getDimensionPixelSize(
                            com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_marginRight, -1);
                    leftMargin = left >= 0 ? left : Math.max(start, 0);
                    rightMargin = right >= 0 ? right : Math.max(end, 0);
                    topMargin = Math.max(a.getDimensionPixelSize(
                            com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_marginTop, 0), 0);
                    bottomMargin = Math.max(a.getDimensionPixelSize(
                            com.android.internal.R.styleable.ViewGroup_MarginLayout_layout_marginBottom, 0), 0);
                }
            } finally {
                a.recycle();
            }
        }

        public MarginLayoutParams() {
            super();
        }

        public MarginLayoutParams(int width, int height) {
            super(width, height);
        }

        public MarginLayoutParams(MarginLayoutParams source) {
            super(source);
            this.leftMargin = source.leftMargin;
            this.topMargin = source.topMargin;
            this.rightMargin = source.rightMargin;
            this.bottomMargin = source.bottomMargin;
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
        public boolean isMarginRelative() { return false; }
    }

    public interface OnHierarchyChangeListener {
        void onChildViewAdded(View parent, View child);
        void onChildViewRemoved(View parent, View child);
    }

    // ViewParent.

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
