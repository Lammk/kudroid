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

    private int mGravity = 0;

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
            } finally {
                a.recycle();
            }
        }
    }

    public void setGravity(int gravity) { mGravity = gravity; }
    public int getGravity() { return mGravity; }

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
        private final int[] mRules = new int[VERB_COUNT];

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
    }
}
