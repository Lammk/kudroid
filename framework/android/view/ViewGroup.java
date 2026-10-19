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
        if (isAttachedToWindow()) {
            child.dispatchAttachedToWindow();
        }
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
        if (isAttachedToWindow()) {
            view.dispatchDetachedFromWindow();
        }
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

        /** Set by layout animation controllers to describe the child's position in its group. */
        public android.view.animation.LayoutAnimationController.AnimationParameters
                layoutAnimationParameters;

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

    // ------------------------------------------------------------------------------------
    // Attach, visibility and window events, forwarded to the children.

    @Override
    void dispatchAttachedToWindow() {
        super.dispatchAttachedToWindow();
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchAttachedToWindow();
        }
    }

    @Override
    void dispatchDetachedFromWindow() {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchDetachedFromWindow();
        }
        super.dispatchDetachedFromWindow();
    }

    @Override
    protected void dispatchVisibilityChanged(View changedView, int visibility) {
        super.dispatchVisibilityChanged(changedView, visibility);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchVisibilityChanged(changedView, visibility);
        }
    }

    @Override
    boolean dispatchVisibilityAggregated(boolean isVisible) {
        isVisible = super.dispatchVisibilityAggregated(isVisible);
        for (int i = 0; i < mChildCount; i++) {
            if (mChildren[i].getVisibility() == VISIBLE) {
                mChildren[i].dispatchVisibilityAggregated(isVisible);
            }
        }
        return isVisible;
    }

    @Override
    public void dispatchWindowVisibilityChanged(int visibility) {
        super.dispatchWindowVisibilityChanged(visibility);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchWindowVisibilityChanged(visibility);
        }
    }

    @Override
    public void dispatchWindowFocusChanged(boolean hasFocus) {
        super.dispatchWindowFocusChanged(hasFocus);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchWindowFocusChanged(hasFocus);
        }
    }

    @Override
    public void dispatchConfigurationChanged(android.content.res.Configuration newConfig) {
        super.dispatchConfigurationChanged(newConfig);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchConfigurationChanged(newConfig);
        }
    }

    @Override
    public void dispatchDisplayHint(int hint) {
        super.dispatchDisplayHint(hint);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchDisplayHint(hint);
        }
    }

    @Override
    public void dispatchSystemUiVisibilityChanged(int visible) {
        super.dispatchSystemUiVisibilityChanged(visible);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchSystemUiVisibilityChanged(visible);
        }
    }

    @Override
    public void dispatchWindowSystemUiVisiblityChanged(int visible) {
        super.dispatchWindowSystemUiVisiblityChanged(visible);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchWindowSystemUiVisiblityChanged(visible);
        }
    }

    @Override
    public void dispatchStartTemporaryDetach() {
        super.dispatchStartTemporaryDetach();
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchStartTemporaryDetach();
        }
    }

    @Override
    public void dispatchFinishTemporaryDetach() {
        super.dispatchFinishTemporaryDetach();
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchFinishTemporaryDetach();
        }
    }

    @Override
    public void dispatchPointerCaptureChanged(boolean hasCapture) {
        super.dispatchPointerCaptureChanged(hasCapture);
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].dispatchPointerCaptureChanged(hasCapture);
        }
    }

    @Override
    public WindowInsets dispatchApplyWindowInsets(WindowInsets insets) {
        insets = super.dispatchApplyWindowInsets(insets);
        if (insets != null && !insets.isConsumed()) {
            for (int i = 0; i < mChildCount; i++) {
                insets = mChildren[i].dispatchApplyWindowInsets(insets);
                if (insets == null || insets.isConsumed()) {
                    break;
                }
            }
        }
        return insets;
    }

    // ------------------------------------------------------------------------------------
    // Child management that does not trigger a layout pass.

    /** Adds a child during layout without asking for another pass. */
    protected boolean addViewInLayout(View child, int index, LayoutParams params) {
        return addViewInLayout(child, index, params, false);
    }

    protected boolean addViewInLayout(View child, int index, LayoutParams params,
            boolean preventRequestLayout) {
        if (child == null) {
            throw new IllegalArgumentException("Cannot add a null child view to a ViewGroup");
        }
        addViewInner(child, index, params);
        if (!preventRequestLayout) {
            requestLayout();
        }
        return true;
    }

    /** Re-attaches a child that detachViewFromParent removed, keeping its state. */
    protected void attachViewToParent(View child, int index, LayoutParams params) {
        child.setLayoutParamsNoRequest(params);
        if (index < 0 || index > mChildCount) {
            index = mChildCount;
        }
        if (mChildCount >= mChildren.length) {
            View[] bigger = new View[mChildren.length * 2];
            System.arraycopy(mChildren, 0, bigger, 0, mChildren.length);
            mChildren = bigger;
        }
        System.arraycopy(mChildren, index, mChildren, index + 1, mChildCount - index);
        mChildren[index] = child;
        mChildCount++;
        child.setParent(this);
    }

    /** Detaches a child without notifying listeners or dispatching detach to it. */
    protected void detachViewFromParent(View child) {
        final int index = indexOfChild(child);
        if (index >= 0) {
            detachViewFromParent(index);
        }
    }

    protected void detachViewFromParent(int index) {
        final View view = mChildren[index];
        System.arraycopy(mChildren, index + 1, mChildren, index, mChildCount - index - 1);
        mChildCount--;
        mChildren[mChildCount] = null;
        view.setParent(null);
    }

    protected void detachViewsFromParent(int start, int count) {
        final int end = Math.min(mChildCount, start + count);
        for (int i = end - 1; i >= start; i--) {
            detachViewFromParent(i);
        }
    }

    protected void detachAllViewsFromParent() {
        for (int i = mChildCount - 1; i >= 0; i--) {
            detachViewFromParent(i);
        }
    }

    protected void removeDetachedView(View child, boolean animate) {
        if (child.getParent() != null) {
            throw new IllegalStateException("View " + child + " being removed from "
                    + "parent but it is still attached");
        }
        child.dispatchDetachedFromWindow();
        onViewRemoved(child);
    }

    public void removeViewsInLayout(int start, int count) {
        removeViews(start, count);
    }

    public void updateViewLayout(View view, LayoutParams params) {
        if (!checkLayoutParams(params)) {
            throw new IllegalArgumentException("Invalid LayoutParams supplied to " + this);
        }
        if (view.getParent() != this) {
            throw new IllegalArgumentException("Given view not a child of " + this);
        }
        view.setLayoutParams(params);
    }

    public void startViewTransition(View view) {
    }

    public void endViewTransition(View view) {
    }

    public void clearDisappearingChildren() {
    }

    protected void cleanupLayoutState(View child) {
    }

    // ------------------------------------------------------------------------------------
    // Layout suppression, transitions and layout animation.

    private boolean mSuppressLayout;
    private boolean mLayoutCalledWhileSuppressed;
    private android.animation.LayoutTransition mTransition;
    private android.view.animation.LayoutAnimationController mLayoutAnimationController;
    private android.view.animation.Animation.AnimationListener mAnimationListener;
    private int mLayoutMode = LAYOUT_MODE_UNDEFINED;
    private boolean mChildrenDrawingOrderEnabled;
    private boolean mStaticTransformationsEnabled;
    private boolean mAnimationCacheEnabled = true;
    private boolean mAlwaysDrawnWithCache;
    private boolean mChildrenDrawnWithCache;
    private int mPersistentDrawingCache = PERSISTENT_SCROLLING_CACHE;
    private boolean mTransitionGroup;
    private boolean mSplitMotionEvents;
    private boolean mTouchscreenBlocksFocus;
    private boolean mAddStatesFromChildren;
    private int mNestedScrollAxes;
    private int mTransientChildren;

    public static final int LAYOUT_MODE_UNDEFINED = -1;
    public static final int LAYOUT_MODE_CLIP_BOUNDS = 0;
    public static final int LAYOUT_MODE_OPTICAL_BOUNDS = 1;
    public static final int PERSISTENT_NO_CACHE = 0x0;
    public static final int PERSISTENT_ANIMATION_CACHE = 0x1;
    public static final int PERSISTENT_SCROLLING_CACHE = 0x2;
    public static final int PERSISTENT_ALL_CACHES = 0x3;

    @Override
    public void layout(int l, int t, int r, int b) {
        if (!mSuppressLayout && (mTransition == null || !mTransition.isChangingLayout())) {
            if (mTransition != null) {
                mTransition.layoutChange(this);
            }
            super.layout(l, t, r, b);
        } else {
            mLayoutCalledWhileSuppressed = true;
        }
    }

    public void suppressLayout(boolean suppress) {
        mSuppressLayout = suppress;
        if (!suppress && mLayoutCalledWhileSuppressed) {
            requestLayout();
            mLayoutCalledWhileSuppressed = false;
        }
    }

    public boolean isLayoutSuppressed() {
        return mSuppressLayout;
    }

    public void setLayoutTransition(android.animation.LayoutTransition transition) {
        mTransition = transition;
    }

    public android.animation.LayoutTransition getLayoutTransition() {
        return mTransition;
    }

    /** Called by a LayoutTransition that wants a pass to start its animations. */
    public void requestTransitionStart(android.animation.LayoutTransition transition) {
        requestLayout();
    }

    public android.view.animation.LayoutAnimationController getLayoutAnimation() {
        return mLayoutAnimationController;
    }

    public void setLayoutAnimation(android.view.animation.LayoutAnimationController controller) {
        mLayoutAnimationController = controller;
    }

    public void scheduleLayoutAnimation() {
    }

    public void startLayoutAnimation() {
    }

    public android.view.animation.Animation.AnimationListener getLayoutAnimationListener() {
        return mAnimationListener;
    }

    public void setLayoutAnimationListener(android.view.animation.Animation.AnimationListener l) {
        mAnimationListener = l;
    }

    protected boolean canAnimate() {
        return mLayoutAnimationController != null;
    }

    protected void attachLayoutAnimationParameters(View child, LayoutParams params, int index,
            int count) {
        android.view.animation.LayoutAnimationController.AnimationParameters animationParams =
                params.layoutAnimationParameters;
        if (animationParams == null) {
            animationParams = new android.view.animation.LayoutAnimationController
                    .AnimationParameters();
            params.layoutAnimationParameters = animationParams;
        }
        animationParams.count = count;
        animationParams.index = index;
    }

    public int getLayoutMode() {
        return mLayoutMode == LAYOUT_MODE_UNDEFINED ? LAYOUT_MODE_CLIP_BOUNDS : mLayoutMode;
    }

    public void setLayoutMode(int layoutMode) {
        if (mLayoutMode != layoutMode) {
            mLayoutMode = layoutMode;
            requestLayout();
        }
    }

    public boolean isTransitionGroup() {
        return mTransitionGroup;
    }

    public void setTransitionGroup(boolean isTransitionGroup) {
        mTransitionGroup = isTransitionGroup;
    }

    public boolean isMotionEventSplittingEnabled() {
        return mSplitMotionEvents;
    }

    public void setMotionEventSplittingEnabled(boolean split) {
        mSplitMotionEvents = split;
    }

    public boolean getTouchscreenBlocksFocus() {
        return mTouchscreenBlocksFocus;
    }

    public void setTouchscreenBlocksFocus(boolean touchscreenBlocksFocus) {
        mTouchscreenBlocksFocus = touchscreenBlocksFocus;
    }

    protected boolean isChildrenDrawingOrderEnabled() {
        return mChildrenDrawingOrderEnabled;
    }

    protected void setChildrenDrawingOrderEnabled(boolean enabled) {
        mChildrenDrawingOrderEnabled = enabled;
    }

    protected int getChildDrawingOrder(int childCount, int drawingPosition) {
        return drawingPosition;
    }

    public final int getChildDrawingOrder(int drawingPosition) {
        return getChildDrawingOrder(mChildCount, drawingPosition);
    }

    protected void setStaticTransformationsEnabled(boolean enabled) {
        mStaticTransformationsEnabled = enabled;
    }

    protected boolean getChildStaticTransformation(View child,
            android.view.animation.Transformation t) {
        return false;
    }

    public boolean isAnimationCacheEnabled() {
        return mAnimationCacheEnabled;
    }

    public void setAnimationCacheEnabled(boolean enabled) {
        mAnimationCacheEnabled = enabled;
    }

    public boolean isAlwaysDrawnWithCacheEnabled() {
        return mAlwaysDrawnWithCache;
    }

    public void setAlwaysDrawnWithCacheEnabled(boolean always) {
        mAlwaysDrawnWithCache = always;
    }

    protected boolean isChildrenDrawnWithCacheEnabled() {
        return mChildrenDrawnWithCache;
    }

    protected void setChildrenDrawnWithCacheEnabled(boolean enabled) {
        mChildrenDrawnWithCache = enabled;
    }

    protected void setChildrenDrawingCacheEnabled(boolean enabled) {
    }

    public int getPersistentDrawingCache() {
        return mPersistentDrawingCache;
    }

    public void setPersistentDrawingCache(int drawingCacheToKeep) {
        mPersistentDrawingCache = drawingCacheToKeep & PERSISTENT_ALL_CACHES;
    }

    protected boolean drawChild(android.graphics.Canvas canvas, View child, long drawingTime) {
        child.draw(canvas);
        return false;
    }

    protected boolean getChildStaticTransformationEnabled() {
        return mStaticTransformationsEnabled;
    }

    public boolean gatherTransparentRegion(android.graphics.Region region) {
        return true;
    }

    public void requestTransparentRegion(View child) {
        if (getParent() != null) {
            ((ViewParent) getParent()).requestTransparentRegion(this);
        }
    }

    public void recomputeViewAttributes(View child) {
    }

    public boolean shouldDelayChildPressedState() {
        return true;
    }

    // ------------------------------------------------------------------------------------
    // Drawable state of the group and its children.

    public boolean addStatesFromChildren() {
        return mAddStatesFromChildren;
    }

    public void setAddStatesFromChildren(boolean addsStates) {
        mAddStatesFromChildren = addsStates;
        refreshDrawableState();
    }

    @Override
    public void childDrawableStateChanged(View child) {
        if (mAddStatesFromChildren) {
            refreshDrawableState();
        }
    }

    @Override
    protected int[] onCreateDrawableState(int extraSpace) {
        if (!mAddStatesFromChildren) {
            return super.onCreateDrawableState(extraSpace);
        }
        int need = 0;
        for (int i = 0; i < mChildCount; i++) {
            final int[] childState = mChildren[i].getDrawableState();
            if (childState != null) {
                need += childState.length;
            }
        }
        final int[] state = super.onCreateDrawableState(extraSpace + need);
        for (int i = 0; i < mChildCount; i++) {
            final int[] childState = mChildren[i].getDrawableState();
            if (childState != null) {
                mergeDrawableStates(state, childState);
            }
        }
        return state;
    }

    @Override
    protected void drawableStateChanged() {
        super.drawableStateChanged();
        for (int i = 0; i < mChildCount; i++) {
            if (mChildren[i].isDuplicateParentStateEnabled()) {
                mChildren[i].refreshDrawableState();
            }
        }
    }

    @Override
    public void jumpDrawablesToCurrentState() {
        super.jumpDrawablesToCurrentState();
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].jumpDrawablesToCurrentState();
        }
    }

    @Override
    protected void dispatchSetSelected(boolean selected) {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].setSelected(selected);
        }
    }

    @Override
    protected void dispatchSetActivated(boolean activated) {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].setActivated(activated);
        }
    }

    @Override
    protected void dispatchSetPressed(boolean pressed) {
        for (int i = 0; i < mChildCount; i++) {
            final View child = mChildren[i];
            // Children that handle clicks themselves keep their own pressed state.
            if (!pressed || (!child.isClickable() && !child.isLongClickable())) {
                child.setPressed(pressed);
            }
        }
    }

    @Override
    public void dispatchDrawableHotspotChanged(float x, float y) {
        for (int i = 0; i < mChildCount; i++) {
            final View child = mChildren[i];
            if (!child.isClickable() && !child.isLongClickable()) {
                child.drawableHotspotChanged(x - child.getLeft() + mScrollX,
                        y - child.getTop() + mScrollY);
            }
        }
    }

    @Override
    public void childHasTransientStateChanged(View child, boolean childHasTransientState) {
        final boolean oldHasTransientState = hasTransientState();
        if (childHasTransientState) {
            mTransientChildren++;
        } else {
            mTransientChildren--;
        }
        final boolean newHasTransientState = hasTransientState();
        if (oldHasTransientState != newHasTransientState && getParentGroup() != null) {
            getParentGroup().childHasTransientStateChanged(this, newHasTransientState);
        }
    }

    @Override
    public boolean hasTransientState() {
        return mTransientChildren > 0 || super.hasTransientState();
    }

    // ------------------------------------------------------------------------------------
    // Saved state of the whole subtree.

    @Override
    protected void dispatchSaveInstanceState(android.util.SparseArray<android.os.Parcelable> container) {
        super.dispatchSaveInstanceState(container);
        for (int i = 0; i < mChildCount; i++) {
            final View c = mChildren[i];
            if (c.isSaveFromParentEnabled()) {
                c.dispatchSaveInstanceState(container);
            }
        }
    }

    protected void dispatchFreezeSelfOnly(android.util.SparseArray<android.os.Parcelable> container) {
        super.dispatchSaveInstanceState(container);
    }

    @Override
    protected void dispatchRestoreInstanceState(
            android.util.SparseArray<android.os.Parcelable> container) {
        super.dispatchRestoreInstanceState(container);
        for (int i = 0; i < mChildCount; i++) {
            final View c = mChildren[i];
            if (c.isSaveFromParentEnabled()) {
                c.dispatchRestoreInstanceState(container);
            }
        }
    }

    protected void dispatchThawSelfOnly(android.util.SparseArray<android.os.Parcelable> container) {
        super.dispatchRestoreInstanceState(container);
    }

    // ------------------------------------------------------------------------------------
    // Focus and key dispatch.

    @Override
    public boolean hasFocus() {
        return super.hasFocus() || getFocusedChild() != null;
    }

    @Override
    public View findFocus() {
        if (super.hasFocus()) {
            return this;
        }
        for (int i = 0; i < mChildCount; i++) {
            final View found = mChildren[i].findFocus();
            if (found != null) {
                return found;
            }
        }
        return null;
    }

    @Override
    public void clearFocus() {
        for (int i = 0; i < mChildCount; i++) {
            mChildren[i].clearFocus();
        }
        super.clearFocus();
    }

    @Override
    public boolean requestFocus(int direction, android.graphics.Rect previouslyFocusedRect) {
        final int descendantFocusability = getDescendantFocusability();
        switch (descendantFocusability) {
            case FOCUS_BLOCK_DESCENDANTS:
                return super.requestFocus(direction, previouslyFocusedRect);
            case FOCUS_BEFORE_DESCENDANTS: {
                final boolean took = super.requestFocus(direction, previouslyFocusedRect);
                return took ? took : onRequestFocusInDescendants(direction, previouslyFocusedRect);
            }
            case FOCUS_AFTER_DESCENDANTS: {
                final boolean took = onRequestFocusInDescendants(direction, previouslyFocusedRect);
                return took ? took : super.requestFocus(direction, previouslyFocusedRect);
            }
            default:
                throw new IllegalStateException("descendant focusability must be one of "
                        + "FOCUS_BEFORE_DESCENDANTS, FOCUS_AFTER_DESCENDANTS, FOCUS_BLOCK_DESCENDANTS "
                        + "but is " + descendantFocusability);
        }
    }

    protected boolean onRequestFocusInDescendants(int direction,
            android.graphics.Rect previouslyFocusedRect) {
        int index;
        int increment;
        int end;
        if ((direction & FOCUS_FORWARD) != 0) {
            index = 0;
            increment = 1;
            end = mChildCount;
        } else {
            index = mChildCount - 1;
            increment = -1;
            end = -1;
        }
        for (int i = index; i != end; i += increment) {
            final View child = mChildren[i];
            if (child.getVisibility() == VISIBLE) {
                if (child.requestFocus(direction, previouslyFocusedRect)) {
                    return true;
                }
            }
        }
        return false;
    }

    @Override
    public boolean restoreDefaultFocus() {
        return requestFocus(FOCUS_FORWARD);
    }

    @Override
    public void addFocusables(java.util.ArrayList<View> views, int direction, int focusableMode) {
        final int focusableCount = views.size();
        final int descendantFocusability = getDescendantFocusability();
        if (descendantFocusability != FOCUS_BLOCK_DESCENDANTS) {
            for (int i = 0; i < mChildCount; i++) {
                final View child = mChildren[i];
                if (child.getVisibility() == VISIBLE) {
                    child.addFocusables(views, direction, focusableMode);
                }
            }
        }
        // When no descendant took part (or the group goes first), the group may add itself.
        if ((descendantFocusability != FOCUS_AFTER_DESCENDANTS) || (focusableCount == views.size())) {
            super.addFocusables(views, direction, focusableMode);
        }
    }

    @Override
    public void addTouchables(java.util.ArrayList<View> views) {
        super.addTouchables(views);
        for (int i = 0; i < mChildCount; i++) {
            final View child = mChildren[i];
            if (child.getVisibility() == VISIBLE) {
                child.addTouchables(views);
            }
        }
    }

    @Override
    public void addKeyboardNavigationClusters(java.util.Collection<View> views, int direction) {
    }

    public void focusableViewAvailable(View v) {
        if (getParentGroup() != null) {
            getParentGroup().focusableViewAvailable(v);
        }
    }

    @Override
    public void findViewsWithText(java.util.ArrayList<View> outViews, CharSequence text, int flags) {
        super.findViewsWithText(outViews, text, flags);
        for (int i = 0; i < mChildCount; i++) {
            final View child = mChildren[i];
            if (child.getVisibility() == VISIBLE) {
                child.findViewsWithText(outViews, text, flags);
            }
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (super.hasFocus()) {
            return super.dispatchKeyEvent(event);
        }
        final View focused = findFocus();
        if (focused != null && focused != this) {
            return focused.dispatchKeyEvent(event);
        }
        return false;
    }

    @Override
    public boolean dispatchKeyEventPreIme(KeyEvent event) {
        final View focused = findFocus();
        if (focused != null && focused != this) {
            return focused.dispatchKeyEventPreIme(event);
        }
        return super.dispatchKeyEventPreIme(event);
    }

    @Override
    public boolean dispatchKeyShortcutEvent(KeyEvent event) {
        final View focused = findFocus();
        if (focused != null && focused != this) {
            return focused.dispatchKeyShortcutEvent(event);
        }
        return super.dispatchKeyShortcutEvent(event);
    }

    @Override
    public boolean dispatchTrackballEvent(MotionEvent event) {
        final View focused = findFocus();
        if (focused != null && focused != this) {
            return focused.dispatchTrackballEvent(event);
        }
        return super.dispatchTrackballEvent(event);
    }

    @Override
    public boolean dispatchUnhandledMove(View focused, int direction) {
        return false;
    }

    public boolean onInterceptHoverEvent(MotionEvent event) {
        return false;
    }

    // ------------------------------------------------------------------------------------
    // ViewParent: geometry, nested scrolling and requests from children.

    public boolean getChildVisibleRect(View child, android.graphics.Rect r,
            android.graphics.Point offset) {
        final android.graphics.RectF rect = new android.graphics.RectF(r);
        if (!child.hasIdentityMatrix()) {
            child.getMatrix().mapRect(rect);
        }
        final int dx = child.getLeft() - mScrollX;
        final int dy = child.getTop() - mScrollY;
        rect.offset(dx, dy);
        if (offset != null) {
            offset.x += dx;
            offset.y += dy;
        }
        final int width = getWidth();
        final int height = getHeight();
        boolean rectIsVisible = true;
        if (getParentGroup() == null || getParentGroup().getClipChildren()) {
            rectIsVisible = rect.intersect(0, 0, width, height);
        }
        if (rectIsVisible && getClipToPadding()) {
            rectIsVisible = rect.intersect(getPaddingLeft(), getPaddingTop(),
                    width - getPaddingRight(), height - getPaddingBottom());
        }
        r.set((int) Math.floor(rect.left), (int) Math.floor(rect.top),
                (int) Math.ceil(rect.right), (int) Math.ceil(rect.bottom));
        if (rectIsVisible && getParentGroup() != null) {
            rectIsVisible = getParentGroup().getChildVisibleRect(this, r, offset);
        }
        return rectIsVisible;
    }

    public final void offsetDescendantRectToMyCoords(View descendant, android.graphics.Rect rect) {
        offsetRectBetweenParentAndChild(descendant, rect, true, false);
    }

    public final void offsetRectIntoDescendantCoords(View descendant, android.graphics.Rect rect) {
        offsetRectBetweenParentAndChild(descendant, rect, false, false);
    }

    void offsetRectBetweenParentAndChild(View descendant, android.graphics.Rect rect,
            boolean offsetFromChildToParent, boolean clipToBounds) {
        if (descendant == this) {
            return;
        }
        ViewParent theParent = descendant.getParent();
        while ((theParent != null) && (theParent instanceof View) && (theParent != this)) {
            final View p = (View) theParent;
            if (offsetFromChildToParent) {
                rect.offset(descendant.getLeft() - descendant.getScrollX(),
                        descendant.getTop() - descendant.getScrollY());
                if (clipToBounds && !rect.intersect(0, 0, p.getWidth(), p.getHeight())) {
                    rect.setEmpty();
                }
            } else {
                if (clipToBounds && !rect.intersect(0, 0, p.getWidth(), p.getHeight())) {
                    rect.setEmpty();
                }
                rect.offset(descendant.getScrollX() - descendant.getLeft(),
                        descendant.getScrollY() - descendant.getTop());
            }
            descendant = p;
            theParent = descendant.getParent();
        }
        if (theParent == this) {
            if (offsetFromChildToParent) {
                rect.offset(descendant.getLeft() - descendant.getScrollX(),
                        descendant.getTop() - descendant.getScrollY());
            } else {
                rect.offset(descendant.getScrollX() - descendant.getLeft(),
                        descendant.getScrollY() - descendant.getTop());
            }
        } else {
            throw new IllegalArgumentException("parameter must be a descendant of this view");
        }
    }

    public boolean requestChildRectangleOnScreen(View child, android.graphics.Rect rectangle,
            boolean immediate) {
        return false;
    }

    public boolean requestSendAccessibilityEvent(View child,
            android.view.accessibility.AccessibilityEvent event) {
        return false;
    }

    public boolean onRequestSendAccessibilityEvent(View child,
            android.view.accessibility.AccessibilityEvent event) {
        return true;
    }

    @Override
    public void notifySubtreeAccessibilityStateChanged(View child, View source, int changeType) {
    }

    public ViewParent invalidateChildInParent(int[] location, android.graphics.Rect dirty) {
        return getParent();
    }

    public void onDescendantInvalidated(View child, View target) {
        invalidate();
    }

    public boolean showContextMenuForChild(View originalView) {
        return getParentGroup() != null && getParentGroup().showContextMenuForChild(originalView);
    }

    public boolean showContextMenuForChild(View originalView, float x, float y) {
        return showContextMenuForChild(originalView);
    }

    public ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback) {
        return startActionModeForChild(originalView, callback, ActionMode.TYPE_PRIMARY);
    }

    public ActionMode startActionModeForChild(View originalView, ActionMode.Callback callback,
            int type) {
        return getParentGroup() != null
                ? getParentGroup().startActionModeForChild(originalView, callback, type) : null;
    }

    public int getNestedScrollAxes() {
        return mNestedScrollAxes;
    }

    public boolean onStartNestedScroll(View child, View target, int nestedScrollAxes) {
        return false;
    }

    public void onNestedScrollAccepted(View child, View target, int axes) {
        mNestedScrollAxes = axes;
    }

    public void onStopNestedScroll(View child) {
        stopNestedScroll();
        mNestedScrollAxes = 0;
    }

    public void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed,
            int dyUnconsumed) {
        dispatchNestedScroll(dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed, null);
    }

    public void onNestedPreScroll(View target, int dx, int dy, int[] consumed) {
        dispatchNestedPreScroll(dx, dy, consumed, null);
    }

    public boolean onNestedFling(View target, float velocityX, float velocityY, boolean consumed) {
        return dispatchNestedFling(velocityX, velocityY, consumed);
    }

    public boolean onNestedPreFling(View target, float velocityX, float velocityY) {
        return dispatchNestedPreFling(velocityX, velocityY);
    }

    public boolean onNestedPrePerformAccessibilityAction(View target, int action,
            android.os.Bundle args) {
        return false;
    }
}
