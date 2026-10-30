package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/**
 * Positions children relative to each other or to the parent. Rules form a dependency graph:
 * horizontal and vertical rules are each resolved in dependency order, so an anchor is always
 * measured before the views that refer to it.
 */
public class RelativeLayout extends ViewGroup {
    public static final int TRUE = -1;
    public static final int LEFT_OF = 0;
    public static final int RIGHT_OF = 1;
    public static final int ABOVE = 2;
    public static final int BELOW = 3;
    public static final int ALIGN_BASELINE = 4;
    public static final int ALIGN_LEFT = 5;
    public static final int ALIGN_TOP = 6;
    public static final int ALIGN_RIGHT = 7;
    public static final int ALIGN_BOTTOM = 8;
    public static final int ALIGN_PARENT_LEFT = 9;
    public static final int ALIGN_PARENT_TOP = 10;
    public static final int ALIGN_PARENT_RIGHT = 11;
    public static final int ALIGN_PARENT_BOTTOM = 12;
    public static final int CENTER_IN_PARENT = 13;
    public static final int CENTER_HORIZONTAL = 14;
    public static final int CENTER_VERTICAL = 15;
    public static final int START_OF = 16;
    public static final int END_OF = 17;
    public static final int ALIGN_START = 18;
    public static final int ALIGN_END = 19;
    public static final int ALIGN_PARENT_START = 20;
    public static final int ALIGN_PARENT_END = 21;
    private static final int VERB_COUNT = 22;

    private static final int VALUE_NOT_SET = Integer.MIN_VALUE;

    private static final int[] RULES_VERTICAL = {ABOVE, BELOW, ALIGN_BASELINE, ALIGN_TOP, ALIGN_BOTTOM};
    private static final int[] RULES_HORIZONTAL = {LEFT_OF, RIGHT_OF, ALIGN_LEFT, ALIGN_RIGHT,
            START_OF, END_OF, ALIGN_START, ALIGN_END};

    private int mGravity = android.view.Gravity.START | android.view.Gravity.TOP;
    private int mIgnoreGravity = View.NO_ID;
    private boolean mDirtyHierarchy = true;
    private View[] mSortedHorizontalChildren = new View[0];
    private View[] mSortedVerticalChildren = new View[0];
    private final android.util.SparseArray<View> mKeyViews = new android.util.SparseArray<View>();
    private final android.graphics.Rect mContentBounds = new android.graphics.Rect();
    private final android.graphics.Rect mSelfBounds = new android.graphics.Rect();

    public RelativeLayout(Context context) {
        this(context, null);
    }

    public RelativeLayout(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public RelativeLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public RelativeLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (context != null && (attrs != null || defStyleAttr != 0 || defStyleRes != 0)) {
            final android.content.res.TypedArray a = context.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.RelativeLayout, defStyleAttr, defStyleRes);
            try {
                mGravity = a.getInt(com.android.internal.R.styleable.RelativeLayout_gravity, mGravity);
                mIgnoreGravity = a.getResourceId(
                        com.android.internal.R.styleable.RelativeLayout_ignoreGravity, View.NO_ID);
            } finally {
                a.recycle();
            }
        }
    }

    public void setGravity(int gravity) {
        if (mGravity != gravity) {
            if ((gravity & android.view.Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK) == 0) {
                gravity |= android.view.Gravity.START;
            }
            if ((gravity & android.view.Gravity.VERTICAL_GRAVITY_MASK) == 0) {
                gravity |= android.view.Gravity.TOP;
            }
            mGravity = gravity;
            requestLayout();
        }
    }

    public int getGravity() { return mGravity; }

    public void setIgnoreGravity(int viewId) { mIgnoreGravity = viewId; }

    public int getIgnoreGravity() { return mIgnoreGravity; }

    @Override
    public void requestLayout() {
        super.requestLayout();
        mDirtyHierarchy = true;
    }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
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

    /** Orders children so every view comes after the views its rules refer to. */
    private View[] sortChildren(int[] rulesFilter) {
        final int count = getChildCount();
        mKeyViews.clear();
        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            if (child.getId() != View.NO_ID) mKeyViews.put(child.getId(), child);
        }
        final java.util.ArrayList<View> sorted = new java.util.ArrayList<View>(count);
        final java.util.HashMap<View, java.util.ArrayList<View>> dependents =
                new java.util.HashMap<View, java.util.ArrayList<View>>();
        final java.util.HashMap<View, Integer> pending = new java.util.HashMap<View, Integer>();
        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            final int[] rules = ((LayoutParams) child.getLayoutParams()).getResolvedRules();
            int deps = 0;
            for (int rule : rulesFilter) {
                final View anchor = rules[rule] != 0 ? mKeyViews.get(rules[rule]) : null;
                if (anchor == null || anchor == child) continue;
                java.util.ArrayList<View> list = dependents.get(anchor);
                if (list == null) {
                    list = new java.util.ArrayList<View>();
                    dependents.put(anchor, list);
                }
                if (!list.contains(child)) {
                    list.add(child);
                    deps++;
                }
            }
            pending.put(child, deps);
        }
        final java.util.ArrayDeque<View> roots = new java.util.ArrayDeque<View>();
        for (int i = 0; i < count; i++) {
            if (pending.get(getChildAt(i)) == 0) roots.add(getChildAt(i));
        }
        while (!roots.isEmpty()) {
            final View view = roots.poll();
            sorted.add(view);
            final java.util.ArrayList<View> list = dependents.get(view);
            if (list == null) continue;
            for (View dependent : list) {
                final int left = pending.get(dependent) - 1;
                pending.put(dependent, left);
                if (left == 0) roots.add(dependent);
            }
        }
        // A cycle cannot be resolved; keep the remaining views in tree order rather than fail.
        for (int i = 0; i < count; i++) {
            if (!sorted.contains(getChildAt(i))) sorted.add(getChildAt(i));
        }
        return sorted.toArray(new View[sorted.size()]);
    }

    private View getRelatedView(int[] rules, int relation) {
        final int id = rules[relation];
        if (id == 0) return null;
        View v = mKeyViews.get(id);
        int hops = 0;
        // A GONE anchor defers to what that anchor was itself anchored to.
        while (v != null && v.getVisibility() == GONE && hops++ < getChildCount()) {
            v = mKeyViews.get(((LayoutParams) v.getLayoutParams()).getResolvedRules()[relation]);
        }
        return v;
    }

    private LayoutParams getRelatedViewParams(int[] rules, int relation) {
        final View v = getRelatedView(rules, relation);
        if (v != null && v.getLayoutParams() instanceof LayoutParams) {
            return (LayoutParams) v.getLayoutParams();
        }
        return null;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        if (mDirtyHierarchy) {
            mDirtyHierarchy = false;
            mSortedHorizontalChildren = sortChildren(RULES_HORIZONTAL);
            mSortedVerticalChildren = sortChildren(RULES_VERTICAL);
        }
        int myWidth = -1;
        int myHeight = -1;
        int width = 0;
        int height = 0;
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        final int widthSize = MeasureSpec.getSize(widthMeasureSpec);
        final int heightSize = MeasureSpec.getSize(heightMeasureSpec);
        if (widthMode != MeasureSpec.UNSPECIFIED) myWidth = widthSize;
        if (heightMode != MeasureSpec.UNSPECIFIED) myHeight = heightSize;
        if (widthMode == MeasureSpec.EXACTLY) width = myWidth;
        if (heightMode == MeasureSpec.EXACTLY) height = myHeight;

        View ignore = null;
        int gravity = mGravity & android.view.Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK;
        final boolean horizontalGravity = gravity != android.view.Gravity.START && gravity != 0;
        gravity = mGravity & android.view.Gravity.VERTICAL_GRAVITY_MASK;
        final boolean verticalGravity = gravity != android.view.Gravity.TOP && gravity != 0;
        int left = Integer.MAX_VALUE;
        int top = Integer.MAX_VALUE;
        int right = Integer.MIN_VALUE;
        int bottom = Integer.MIN_VALUE;
        boolean offsetHorizontalAxis = false;
        boolean offsetVerticalAxis = false;
        if ((horizontalGravity || verticalGravity) && mIgnoreGravity != View.NO_ID) {
            ignore = findViewById(mIgnoreGravity);
        }
        final boolean isWrapContentWidth = widthMode != MeasureSpec.EXACTLY;
        final boolean isWrapContentHeight = heightMode != MeasureSpec.EXACTLY;

        View[] views = mSortedHorizontalChildren;
        for (View child : views) {
            if (child.getVisibility() == GONE) continue;
            final LayoutParams params = (LayoutParams) child.getLayoutParams();
            final int[] rules = params.getResolvedRules();
            applyHorizontalSizeRules(params, myWidth, rules);
            measureChildHorizontal(child, params, myWidth, myHeight);
            if (positionChildHorizontal(child, params, myWidth, isWrapContentWidth, rules)) {
                offsetHorizontalAxis = true;
            }
        }

        views = mSortedVerticalChildren;
        for (View child : views) {
            if (child.getVisibility() == GONE) continue;
            final LayoutParams params = (LayoutParams) child.getLayoutParams();
            final int[] rules = params.getResolvedRules();
            applyVerticalSizeRules(params, myHeight, child.getBaseline(), rules);
            measureChild(child, params, myWidth, myHeight);
            if (positionChildVertical(child, params, myHeight, isWrapContentHeight, rules)) {
                offsetVerticalAxis = true;
            }
            if (isWrapContentWidth) width = Math.max(width, params.mRight);
            if (isWrapContentHeight) height = Math.max(height, params.mBottom);
            if (child != ignore || verticalGravity) {
                left = Math.min(left, params.mLeft - params.leftMargin);
                top = Math.min(top, params.mTop - params.topMargin);
            }
            if (child != ignore || horizontalGravity) {
                right = Math.max(right, params.mRight + params.rightMargin);
                bottom = Math.max(bottom, params.mBottom + params.bottomMargin);
            }
        }

        if (isWrapContentWidth) {
            // Width already holds the left padding: it was taken from each child's right edge.
            width += getPaddingRight();
            final ViewGroup.LayoutParams own = getLayoutParams();
            if (own != null && own.width >= 0) width = Math.max(width, own.width);
            width = Math.max(width, getSuggestedMinimumWidth());
            width = resolveSize(width, widthMeasureSpec);
            if (offsetHorizontalAxis) {
                for (View child : mSortedHorizontalChildren) {
                    if (child.getVisibility() == GONE) continue;
                    final LayoutParams params = (LayoutParams) child.getLayoutParams();
                    final int[] rules = params.getResolvedRules();
                    if (rules[CENTER_IN_PARENT] != 0 || rules[CENTER_HORIZONTAL] != 0) {
                        centerHorizontal(child, params, width);
                    } else if (rules[ALIGN_PARENT_RIGHT] != 0) {
                        final int childWidth = child.getMeasuredWidth();
                        params.mLeft = width - getPaddingRight() - childWidth;
                        params.mRight = params.mLeft + childWidth;
                    }
                }
            }
        }

        if (isWrapContentHeight) {
            height += getPaddingBottom();
            final ViewGroup.LayoutParams own = getLayoutParams();
            if (own != null && own.height >= 0) height = Math.max(height, own.height);
            height = Math.max(height, getSuggestedMinimumHeight());
            height = resolveSize(height, heightMeasureSpec);
            if (offsetVerticalAxis) {
                for (View child : mSortedVerticalChildren) {
                    if (child.getVisibility() == GONE) continue;
                    final LayoutParams params = (LayoutParams) child.getLayoutParams();
                    final int[] rules = params.getResolvedRules();
                    if (rules[CENTER_IN_PARENT] != 0 || rules[CENTER_VERTICAL] != 0) {
                        centerVertical(child, params, height);
                    } else if (rules[ALIGN_PARENT_BOTTOM] != 0) {
                        final int childHeight = child.getMeasuredHeight();
                        params.mTop = height - getPaddingBottom() - childHeight;
                        params.mBottom = params.mTop + childHeight;
                    }
                }
            }
        }

        if (horizontalGravity || verticalGravity) {
            final android.graphics.Rect selfBounds = mSelfBounds;
            selfBounds.set(getPaddingLeft(), getPaddingTop(), width - getPaddingRight(), height - getPaddingBottom());
            final android.graphics.Rect contentBounds = mContentBounds;
            android.view.Gravity.apply(mGravity, right - left, bottom - top, selfBounds, contentBounds,
                    getLayoutDirection());
            final int horizontalOffset = contentBounds.left - left;
            final int verticalOffset = contentBounds.top - top;
            if (horizontalOffset != 0 || verticalOffset != 0) {
                for (View child : mSortedVerticalChildren) {
                    if (child.getVisibility() == GONE || child == ignore) continue;
                    final LayoutParams params = (LayoutParams) child.getLayoutParams();
                    if (horizontalGravity) {
                        params.mLeft += horizontalOffset;
                        params.mRight += horizontalOffset;
                    }
                    if (verticalGravity) {
                        params.mTop += verticalOffset;
                        params.mBottom += verticalOffset;
                    }
                }
            }
        }
        setMeasuredDimension(width, height);
    }

    private void measureChild(View child, LayoutParams params, int myWidth, int myHeight) {
        final int childWidthMeasureSpec = getChildMeasureSpec(params.mLeft, params.mRight, params.width,
                params.leftMargin, params.rightMargin, getPaddingLeft(), getPaddingRight(), myWidth);
        final int childHeightMeasureSpec = getChildMeasureSpec(params.mTop, params.mBottom, params.height,
                params.topMargin, params.bottomMargin, getPaddingTop(), getPaddingBottom(), myHeight);
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    private void measureChildHorizontal(View child, LayoutParams params, int myWidth, int myHeight) {
        final int childWidthMeasureSpec = getChildMeasureSpec(params.mLeft, params.mRight, params.width,
                params.leftMargin, params.rightMargin, getPaddingLeft(), getPaddingRight(), myWidth);
        final int childHeightMeasureSpec;
        if (myHeight < 0) {
            if (params.height >= 0) {
                childHeightMeasureSpec = MeasureSpec.makeMeasureSpec(params.height, MeasureSpec.EXACTLY);
            } else {
                childHeightMeasureSpec = MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED);
            }
        } else {
            final int maxHeight = Math.max(0, myHeight - getPaddingTop() - getPaddingBottom()
                    - params.topMargin - params.bottomMargin);
            final int heightMode = params.height == LayoutParams.MATCH_PARENT
                    ? MeasureSpec.EXACTLY : MeasureSpec.AT_MOST;
            childHeightMeasureSpec = MeasureSpec.makeMeasureSpec(maxHeight, heightMode);
        }
        child.measure(childWidthMeasureSpec, childHeightMeasureSpec);
    }

    private int getChildMeasureSpec(int childStart, int childEnd, int childSize, int startMargin,
            int endMargin, int startPadding, int endPadding, int mySize) {
        int childSpecMode = 0;
        int childSpecSize = 0;
        final boolean isUnspecified = mySize < 0;
        if (isUnspecified) {
            if (childStart != VALUE_NOT_SET && childEnd != VALUE_NOT_SET) {
                childSpecSize = Math.max(0, childEnd - childStart);
                childSpecMode = MeasureSpec.EXACTLY;
            } else if (childSize >= 0) {
                childSpecSize = childSize;
                childSpecMode = MeasureSpec.EXACTLY;
            } else {
                childSpecSize = 0;
                childSpecMode = MeasureSpec.UNSPECIFIED;
            }
            return MeasureSpec.makeMeasureSpec(childSpecSize, childSpecMode);
        }
        int tempStart = childStart;
        int tempEnd = childEnd;
        if (tempStart == VALUE_NOT_SET) tempStart = startPadding + startMargin;
        if (tempEnd == VALUE_NOT_SET) tempEnd = mySize - endPadding - endMargin;
        final int maxAvailable = tempEnd - tempStart;
        if (childStart != VALUE_NOT_SET && childEnd != VALUE_NOT_SET) {
            childSpecMode = MeasureSpec.EXACTLY;
            childSpecSize = Math.max(0, maxAvailable);
        } else if (childSize >= 0) {
            childSpecMode = MeasureSpec.EXACTLY;
            childSpecSize = maxAvailable >= 0 ? Math.min(maxAvailable, childSize) : childSize;
        } else if (childSize == LayoutParams.MATCH_PARENT) {
            childSpecMode = MeasureSpec.EXACTLY;
            childSpecSize = Math.max(0, maxAvailable);
        } else if (childSize == LayoutParams.WRAP_CONTENT) {
            if (maxAvailable >= 0) {
                childSpecMode = MeasureSpec.AT_MOST;
                childSpecSize = maxAvailable;
            } else {
                childSpecMode = MeasureSpec.UNSPECIFIED;
                childSpecSize = 0;
            }
        }
        return MeasureSpec.makeMeasureSpec(childSpecSize, childSpecMode);
    }

    private boolean positionChildHorizontal(View child, LayoutParams params, int myWidth,
            boolean wrapContent, int[] rules) {
        if (params.mLeft == VALUE_NOT_SET && params.mRight != VALUE_NOT_SET) {
            params.mLeft = params.mRight - child.getMeasuredWidth();
        } else if (params.mLeft != VALUE_NOT_SET && params.mRight == VALUE_NOT_SET) {
            params.mRight = params.mLeft + child.getMeasuredWidth();
        } else if (params.mLeft == VALUE_NOT_SET && params.mRight == VALUE_NOT_SET) {
            if (rules[CENTER_IN_PARENT] != 0 || rules[CENTER_HORIZONTAL] != 0) {
                if (!wrapContent) {
                    centerHorizontal(child, params, myWidth);
                } else {
                    params.mLeft = getPaddingLeft() + params.leftMargin;
                    params.mRight = params.mLeft + child.getMeasuredWidth();
                }
                return true;
            }
            params.mLeft = getPaddingLeft() + params.leftMargin;
            params.mRight = params.mLeft + child.getMeasuredWidth();
        }
        return rules[ALIGN_PARENT_RIGHT] != 0;
    }

    private boolean positionChildVertical(View child, LayoutParams params, int myHeight,
            boolean wrapContent, int[] rules) {
        if (params.mTop == VALUE_NOT_SET && params.mBottom != VALUE_NOT_SET) {
            params.mTop = params.mBottom - child.getMeasuredHeight();
        } else if (params.mTop != VALUE_NOT_SET && params.mBottom == VALUE_NOT_SET) {
            params.mBottom = params.mTop + child.getMeasuredHeight();
        } else if (params.mTop == VALUE_NOT_SET && params.mBottom == VALUE_NOT_SET) {
            if (rules[CENTER_IN_PARENT] != 0 || rules[CENTER_VERTICAL] != 0) {
                if (!wrapContent) {
                    centerVertical(child, params, myHeight);
                } else {
                    params.mTop = getPaddingTop() + params.topMargin;
                    params.mBottom = params.mTop + child.getMeasuredHeight();
                }
                return true;
            }
            params.mTop = getPaddingTop() + params.topMargin;
            params.mBottom = params.mTop + child.getMeasuredHeight();
        }
        return rules[ALIGN_PARENT_BOTTOM] != 0;
    }

    private void centerHorizontal(View child, LayoutParams params, int myWidth) {
        final int childWidth = child.getMeasuredWidth();
        final int left = (myWidth - childWidth) / 2;
        params.mLeft = left;
        params.mRight = left + childWidth;
    }

    private void centerVertical(View child, LayoutParams params, int myHeight) {
        final int childHeight = child.getMeasuredHeight();
        final int top = (myHeight - childHeight) / 2;
        params.mTop = top;
        params.mBottom = top + childHeight;
    }

    private void applyHorizontalSizeRules(LayoutParams childParams, int myWidth, int[] rules) {
        childParams.mLeft = VALUE_NOT_SET;
        childParams.mRight = VALUE_NOT_SET;
        LayoutParams anchorParams = getRelatedViewParams(rules, LEFT_OF);
        if (anchorParams != null) {
            childParams.mRight = anchorParams.mLeft - (anchorParams.leftMargin + childParams.rightMargin);
        } else if (childParams.alignWithParent && rules[LEFT_OF] != 0) {
            if (myWidth >= 0) childParams.mRight = myWidth - getPaddingRight() - childParams.rightMargin;
        }
        anchorParams = getRelatedViewParams(rules, RIGHT_OF);
        if (anchorParams != null) {
            childParams.mLeft = anchorParams.mRight + (anchorParams.rightMargin + childParams.leftMargin);
        } else if (childParams.alignWithParent && rules[RIGHT_OF] != 0) {
            childParams.mLeft = getPaddingLeft() + childParams.leftMargin;
        }
        anchorParams = getRelatedViewParams(rules, ALIGN_LEFT);
        if (anchorParams != null) {
            childParams.mLeft = anchorParams.mLeft + childParams.leftMargin;
        } else if (childParams.alignWithParent && rules[ALIGN_LEFT] != 0) {
            childParams.mLeft = getPaddingLeft() + childParams.leftMargin;
        }
        anchorParams = getRelatedViewParams(rules, ALIGN_RIGHT);
        if (anchorParams != null) {
            childParams.mRight = anchorParams.mRight - childParams.rightMargin;
        } else if (childParams.alignWithParent && rules[ALIGN_RIGHT] != 0) {
            if (myWidth >= 0) childParams.mRight = myWidth - getPaddingRight() - childParams.rightMargin;
        }
        if (rules[ALIGN_PARENT_LEFT] != 0) {
            childParams.mLeft = getPaddingLeft() + childParams.leftMargin;
        }
        if (rules[ALIGN_PARENT_RIGHT] != 0) {
            if (myWidth >= 0) childParams.mRight = myWidth - getPaddingRight() - childParams.rightMargin;
        }
    }

    private void applyVerticalSizeRules(LayoutParams childParams, int myHeight, int myBaseline, int[] rules) {
        childParams.mTop = VALUE_NOT_SET;
        childParams.mBottom = VALUE_NOT_SET;
        LayoutParams anchorParams = getRelatedViewParams(rules, ABOVE);
        if (anchorParams != null) {
            childParams.mBottom = anchorParams.mTop - (anchorParams.topMargin + childParams.bottomMargin);
        } else if (childParams.alignWithParent && rules[ABOVE] != 0) {
            if (myHeight >= 0) childParams.mBottom = myHeight - getPaddingBottom() - childParams.bottomMargin;
        }
        anchorParams = getRelatedViewParams(rules, BELOW);
        if (anchorParams != null) {
            childParams.mTop = anchorParams.mBottom + (anchorParams.bottomMargin + childParams.topMargin);
        } else if (childParams.alignWithParent && rules[BELOW] != 0) {
            childParams.mTop = getPaddingTop() + childParams.topMargin;
        }
        anchorParams = getRelatedViewParams(rules, ALIGN_TOP);
        if (anchorParams != null) {
            childParams.mTop = anchorParams.mTop + childParams.topMargin;
        } else if (childParams.alignWithParent && rules[ALIGN_TOP] != 0) {
            childParams.mTop = getPaddingTop() + childParams.topMargin;
        }
        anchorParams = getRelatedViewParams(rules, ALIGN_BOTTOM);
        if (anchorParams != null) {
            childParams.mBottom = anchorParams.mBottom - childParams.bottomMargin;
        } else if (childParams.alignWithParent && rules[ALIGN_BOTTOM] != 0) {
            if (myHeight >= 0) childParams.mBottom = myHeight - getPaddingBottom() - childParams.bottomMargin;
        }
        if (rules[ALIGN_PARENT_TOP] != 0) {
            childParams.mTop = getPaddingTop() + childParams.topMargin;
        }
        if (rules[ALIGN_PARENT_BOTTOM] != 0) {
            if (myHeight >= 0) childParams.mBottom = myHeight - getPaddingBottom() - childParams.bottomMargin;
        }
        if (rules[ALIGN_BASELINE] != 0 && myBaseline != -1) {
            final View anchor = getRelatedView(rules, ALIGN_BASELINE);
            anchorParams = getRelatedViewParams(rules, ALIGN_BASELINE);
            if (anchor != null && anchorParams != null) {
                final int anchorBaseline = anchor.getBaseline();
                if (anchorBaseline != -1) {
                    childParams.mTop = anchorParams.mTop + anchorBaseline - myBaseline;
                }
            }
        }
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        final int count = getChildCount();
        for (int i = 0; i < count; i++) {
            final View child = getChildAt(i);
            if (child.getVisibility() != GONE) {
                final LayoutParams st = (LayoutParams) child.getLayoutParams();
                child.layout(st.mLeft, st.mTop, st.mRight, st.mBottom);
            }
        }
    }

    public static class LayoutParams extends ViewGroup.MarginLayoutParams {
        private final int[] mRules = new int[VERB_COUNT];

        int mLeft, mTop, mRight, mBottom;

        /** Treat a missing anchor as the parent edge. */
        public boolean alignWithParent;

        public LayoutParams(Context c, android.util.AttributeSet attrs) {
            super(c, attrs);
            final android.content.res.TypedArray a = c.obtainStyledAttributes(attrs,
                    com.android.internal.R.styleable.RelativeLayout_Layout);
            try {
                final int[] rules = mRules;
                final int n = a.getIndexCount();
                for (int i = 0; i < n; i++) {
                    final int attr = a.getIndex(i);
                    if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_toLeftOf) {
                        rules[LEFT_OF] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_toRightOf) {
                        rules[RIGHT_OF] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_above) {
                        rules[ABOVE] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_below) {
                        rules[BELOW] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignBaseline) {
                        rules[ALIGN_BASELINE] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignLeft) {
                        rules[ALIGN_LEFT] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignTop) {
                        rules[ALIGN_TOP] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignRight) {
                        rules[ALIGN_RIGHT] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignBottom) {
                        rules[ALIGN_BOTTOM] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignParentLeft) {
                        rules[ALIGN_PARENT_LEFT] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignParentTop) {
                        rules[ALIGN_PARENT_TOP] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignParentRight) {
                        rules[ALIGN_PARENT_RIGHT] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignParentBottom) {
                        rules[ALIGN_PARENT_BOTTOM] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_centerInParent) {
                        rules[CENTER_IN_PARENT] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_centerHorizontal) {
                        rules[CENTER_HORIZONTAL] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_centerVertical) {
                        rules[CENTER_VERTICAL] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignWithParentIfMissing) {
                        alignWithParent = a.getBoolean(attr, false);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_toStartOf) {
                        rules[START_OF] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_toEndOf) {
                        rules[END_OF] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignStart) {
                        rules[ALIGN_START] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignEnd) {
                        rules[ALIGN_END] = a.getResourceId(attr, 0);
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignParentStart) {
                        rules[ALIGN_PARENT_START] = a.getBoolean(attr, false) ? TRUE : 0;
                    } else if (attr == com.android.internal.R.styleable.RelativeLayout_Layout_layout_alignParentEnd) {
                        rules[ALIGN_PARENT_END] = a.getBoolean(attr, false) ? TRUE : 0;
                    }
                }
            } finally {
                a.recycle();
            }
        }

        public LayoutParams(int width, int height) {
            super(width, height);
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public LayoutParams(ViewGroup.MarginLayoutParams source) {
            super(source);
        }

        public LayoutParams(LayoutParams source) {
            super((ViewGroup.MarginLayoutParams) source);
            System.arraycopy(source.mRules, 0, mRules, 0, VERB_COUNT);
            this.alignWithParent = source.alignWithParent;
        }

        public void addRule(int verb) {
            addRule(verb, TRUE);
        }

        public void addRule(int verb, int anchor) {
            if (verb >= 0 && verb < VERB_COUNT) mRules[verb] = anchor;
        }

        public void removeRule(int verb) {
            addRule(verb, 0);
        }

        public int getRule(int verb) {
            return verb >= 0 && verb < VERB_COUNT ? mRules[verb] : 0;
        }

        public int[] getRules() {
            return mRules;
        }

        /** The rules with start/end mapped onto left/right (layout direction is always LTR). */
        int[] getResolvedRules() {
            final int[] r = mRules.clone();
            if (r[LEFT_OF] == 0) r[LEFT_OF] = r[START_OF];
            if (r[RIGHT_OF] == 0) r[RIGHT_OF] = r[END_OF];
            if (r[ALIGN_LEFT] == 0) r[ALIGN_LEFT] = r[ALIGN_START];
            if (r[ALIGN_RIGHT] == 0) r[ALIGN_RIGHT] = r[ALIGN_END];
            if (r[ALIGN_PARENT_LEFT] == 0) r[ALIGN_PARENT_LEFT] = r[ALIGN_PARENT_START];
            if (r[ALIGN_PARENT_RIGHT] == 0) r[ALIGN_PARENT_RIGHT] = r[ALIGN_PARENT_END];
            return r;
        }
    }
}
