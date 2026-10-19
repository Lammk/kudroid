package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.os.Bundle;

/**
 * minimal android.view.view implementation.
 *
 * base class for all ui widgets. for kudroid minimal framework, this
 * provides basic layout/drawing simulations.
 */
public class View implements KeyEvent.Callback, android.graphics.drawable.Drawable.Callback {
    /** view visibility: visible. */
    public static final int VISIBLE = 0;
    /** view visibility: invisible. */
    public static final int INVISIBLE = 4;
    /** view visibility: disappears. */
    public static final int GONE = 8;

    /** Used to mark a View that has no ID. */
    public static final int NO_ID = -1;

    /** measurement specification mode: not specified. */
    public static final int UNSPECIFIED = 0;
    /** measurement specification mode: exact. */
    public static final int EXACTLY = 1;
    /** measurement specification mode: most. */
    public static final int AT_MOST = 2;

    protected final Context mContext;
    private int mLeft;
    private int mTop;
    private int mRight;
    private int mBottom;
    private int mVisibility = VISIBLE;
    private int mId = -1;
    private ViewGroup mParent;
    private android.os.Handler mRunQueue;
    private OnClickListener mOnClickListener;
    private OnLongClickListener mOnLongClickListener;
    private OnTouchListener mOnTouchListener;
    private OnFocusChangeListener mOnFocusChangeListener;
    private OnKeyListener mOnKeyListener;
    private AccessibilityDelegate mAccessibilityDelegate;
    private ViewTreeObserver mViewTreeObserver;
    private boolean mHasFocus;
    private long mTouchDownTime;
    private boolean mLongClickFired;
    private boolean mLaidOut;
    private java.util.ArrayList<OnLayoutChangeListener> mLayoutChangeListeners;
    private int mMeasuredWidth;
    private int mMeasuredHeight;
    protected int mScrollX = 0;
    protected int mScrollY = 0;
    protected int mPaddingLeft;
    protected int mPaddingTop;
    protected int mPaddingRight;
    protected int mPaddingBottom;
    private int mMinWidth;
    private int mMinHeight;
    private boolean mEnabled = true;
    private android.graphics.drawable.Drawable mForeground;
    private boolean mClickable;
    private boolean mLongClickable;
    private boolean mFocusable = true;
    private boolean mFocusableInTouchMode;
    private boolean mSelected;
    private boolean mPressed;
    private boolean mActivated;
    private float mAlpha = 1.0f;
    private float mTranslationX;
    private float mTranslationY;
    private float mTranslationZ;
    private float mElevation;
    private float mScaleX = 1.0f;
    private float mScaleY = 1.0f;
    private float mRotation;
    private float mPivotX;
    private float mPivotY;
    private boolean mPivotSet;

    /** Android long press threshold (ViewConfiguration.getLongPressTimeout). */
    private static final long LONG_PRESS_TIMEOUT = 500L;

    /**
     * interface for click callbacks.
     */
    public interface OnClickListener {
        void onClick(View v);
    }

    /**
     * Callback long press. Returns true if processed (blocked onClick).
     */
    public interface OnLongClickListener {
        boolean onLongClick(View v);
    }

    /**
     * Raw touch callback. Return true to consume the event.
     */
    public interface OnTouchListener {
        boolean onTouch(View v, MotionEvent event);
    }

    /**
     * Callback when view receives/loses focus.
     */
    public interface OnFocusChangeListener {
        void onFocusChange(View v, boolean hasFocus);
    }

    public interface OnLayoutChangeListener {
        void onLayoutChange(View v, int left, int top, int right, int bottom,
                            int oldLeft, int oldTop, int oldRight, int oldBottom);
    }

    public void addOnLayoutChangeListener(OnLayoutChangeListener listener) {
        if (listener == null) return;
        if (mLayoutChangeListeners == null) {
            mLayoutChangeListeners = new java.util.ArrayList<OnLayoutChangeListener>();
        }
        if (!mLayoutChangeListeners.contains(listener)) mLayoutChangeListeners.add(listener);
    }

    public void removeOnLayoutChangeListener(OnLayoutChangeListener listener) {
        if (mLayoutChangeListeners != null) mLayoutChangeListeners.remove(listener);
    }

    /**
     * Drag-and-drop callback. Returns true if DragEvent has been processed.
     */
    public interface OnDragListener {
        boolean onDrag(View v, DragEvent event);
    }

    /**
     * Hard key callback sent to the currently focused view.
     */
    public interface OnKeyListener {
        boolean onKey(View v, int keyCode, KeyEvent event);
    }

    /**
     * Callback applies window insets (status bar / notch).
     */
    public interface OnApplyWindowInsetsListener {
        WindowInsets onApplyWindowInsets(View v, WindowInsets insets);
    }

    /**
     * Callback when view is attached/detached from window.
     */
    public interface OnAttachStateChangeListener {
        void onViewAttachedToWindow(View v);

        void onViewDetachedFromWindow(View v);
    }

    /**
     * Provide shadow bitmap when dragging view. Android: abstract class nested in View.
     */
    public static class DragShadowBuilder {
        private final View mView;

        public DragShadowBuilder(View view) {
            mView = view;
        }

        public DragShadowBuilder() {
            mView = null;
        }

        public View getView() {
            return mView;
        }

        public void onProvideShadowMetrics(android.graphics.Point outShadowSize,
                                          android.graphics.Point outShadowTouchPoint) {
            if (outShadowSize != null && mView != null) {
                outShadowSize.set(Math.max(1, mView.getWidth()), Math.max(1, mView.getHeight()));
            }
            if (outShadowTouchPoint != null && outShadowSize != null) {
                outShadowTouchPoint.set(outShadowSize.x / 2, outShadowSize.y / 2);
            }
        }

        public void onDrawShadow(android.graphics.Canvas canvas) {
            if (mView != null && canvas != null) mView.draw(canvas);
        }
    }

    /**
     * Intercept/direct accessibility events. All default functions delegate to views.
     */
    public static class AccessibilityDelegate {
        public void sendAccessibilityEvent(View host, int eventType) {
            host.sendAccessibilityEventInternal(eventType);
        }

        public boolean performAccessibilityAction(View host, int action, Bundle args) {
            return false;
        }

        public void sendAccessibilityEventUnchecked(View host,
                android.view.accessibility.AccessibilityEvent event) {
            host.sendAccessibilityEventUnchecked(event);
        }

        public boolean dispatchPopulateAccessibilityEvent(View host,
                android.view.accessibility.AccessibilityEvent event) {
            return host.dispatchPopulateAccessibilityEvent(event);
        }

        public void onPopulateAccessibilityEvent(View host,
                android.view.accessibility.AccessibilityEvent event) {
            host.onPopulateAccessibilityEvent(event);
        }

        public void onInitializeAccessibilityEvent(View host,
                android.view.accessibility.AccessibilityEvent event) {
            host.onInitializeAccessibilityEvent(event);
        }

        public void onInitializeAccessibilityNodeInfo(View host,
                android.view.accessibility.AccessibilityNodeInfo info) {
            host.onInitializeAccessibilityNodeInfo(info);
        }

        public void addExtraDataToAccessibilityNodeInfo(View host,
                android.view.accessibility.AccessibilityNodeInfo info, String extraDataKey,
                Bundle arguments) {
        }

        public boolean onRequestSendAccessibilityEvent(ViewGroup host, View child,
                android.view.accessibility.AccessibilityEvent event) {
            return true;
        }

        public android.view.accessibility.AccessibilityNodeProvider getAccessibilityNodeProvider(
                View host) {
            return null;
        }
    }

    public View(Context context) {
        this(context, null);
    }

    public View(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public View(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public View(Context context, android.util.AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        mContext = context;
        // A programmatic view (no attributes, no default style) has nothing to read.
        if (context != null && (attrs != null || defStyleAttr != 0 || defStyleRes != 0)) {
            final android.content.res.TypedArray a = context.obtainStyledAttributes(
                    attrs, com.android.internal.R.styleable.View, defStyleAttr, defStyleRes);
            try {
                applyViewAttributes(a);
            } finally {
                a.recycle();
            }
        }
    }

    // The View styleable: what XML and styles may set on every view.
    private void applyViewAttributes(android.content.res.TypedArray a) {
        int leftPadding = -1, topPadding = -1, rightPadding = -1, bottomPadding = -1;
        int padding = -1, paddingHorizontal = -1, paddingVertical = -1;
        int startPadding = -1, endPadding = -1;
        boolean fitsSystemWindows = false;
        final int n = a.getIndexCount();
        for (int i = 0; i < n; i++) {
            final int attr = a.getIndex(i);
            switch (attr) {
                case com.android.internal.R.styleable.View_background:
                    setBackground(a.getDrawable(attr));
                    break;
                case com.android.internal.R.styleable.View_padding:
                    padding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingHorizontal:
                    paddingHorizontal = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingVertical:
                    paddingVertical = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingLeft:
                    leftPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingTop:
                    topPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingRight:
                    rightPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingBottom:
                    bottomPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingStart:
                    startPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_paddingEnd:
                    endPadding = a.getDimensionPixelSize(attr, -1);
                    break;
                case com.android.internal.R.styleable.View_id:
                    mId = a.getResourceId(attr, NO_ID);
                    break;
                case com.android.internal.R.styleable.View_tag:
                    mTag = a.getText(attr);
                    break;
                case com.android.internal.R.styleable.View_visibility:
                    mVisibility = a.getInt(attr, VISIBLE);
                    break;
                case com.android.internal.R.styleable.View_minWidth:
                    mMinWidth = a.getDimensionPixelSize(attr, 0);
                    break;
                case com.android.internal.R.styleable.View_minHeight:
                    mMinHeight = a.getDimensionPixelSize(attr, 0);
                    break;
                case com.android.internal.R.styleable.View_foreground:
                    mForeground = a.getDrawable(attr);
                    break;
                case com.android.internal.R.styleable.View_clickable:
                    mClickable = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_longClickable:
                    mLongClickable = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_focusable:
                    mFocusable = a.getBoolean(attr, true);
                    break;
                case com.android.internal.R.styleable.View_focusableInTouchMode:
                    mFocusableInTouchMode = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_fitsSystemWindows:
                    fitsSystemWindows = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_keepScreenOn:
                    mKeepScreenOn = a.getBoolean(attr, false);
                    break;
                case com.android.internal.R.styleable.View_contentDescription:
                    mContentDescription = a.getText(attr);
                    break;
                case com.android.internal.R.styleable.View_alpha:
                    mAlpha = a.getFloat(attr, 1.0f);
                    break;
                case com.android.internal.R.styleable.View_elevation:
                    mElevation = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_translationX:
                    mTranslationX = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_translationY:
                    mTranslationY = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_translationZ:
                    mTranslationZ = a.getDimension(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_scaleX:
                    mScaleX = a.getFloat(attr, 1.0f);
                    break;
                case com.android.internal.R.styleable.View_scaleY:
                    mScaleY = a.getFloat(attr, 1.0f);
                    break;
                case com.android.internal.R.styleable.View_rotation:
                    mRotation = a.getFloat(attr, 0f);
                    break;
                case com.android.internal.R.styleable.View_onClick:
                    final String handlerName = a.getString(attr);
                    if (handlerName != null) setOnClickListener(new DeclaredOnClickListener(this, handlerName));
                    break;
                default:
                    break;
            }
        }
        if (padding >= 0) {
            leftPadding = topPadding = rightPadding = bottomPadding = padding;
        }
        if (paddingHorizontal >= 0) leftPadding = rightPadding = paddingHorizontal;
        if (paddingVertical >= 0) topPadding = bottomPadding = paddingVertical;
        if (startPadding >= 0) leftPadding = startPadding;
        if (endPadding >= 0) rightPadding = endPadding;
        mPaddingLeft = leftPadding >= 0 ? leftPadding : mPaddingLeft;
        mPaddingTop = topPadding >= 0 ? topPadding : mPaddingTop;
        mPaddingRight = rightPadding >= 0 ? rightPadding : mPaddingRight;
        mPaddingBottom = bottomPadding >= 0 ? bottomPadding : mPaddingBottom;
        if (fitsSystemWindows) mFitsSystemWindows = true;
    }

    // android:onClick="method": resolved against the view's context when first clicked.
    private static final class DeclaredOnClickListener implements OnClickListener {
        private final View mHostView;
        private final String mMethodName;
        private java.lang.reflect.Method mResolvedMethod;
        private Context mResolvedContext;

        DeclaredOnClickListener(View hostView, String methodName) {
            mHostView = hostView;
            mMethodName = methodName;
        }

        @Override
        public void onClick(View v) {
            if (mResolvedMethod == null) resolveMethod(mHostView.getContext());
            try {
                mResolvedMethod.invoke(mResolvedContext, v);
            } catch (IllegalAccessException e) {
                throw new IllegalStateException("Could not execute non-public method of the activity", e);
            } catch (java.lang.reflect.InvocationTargetException e) {
                throw new IllegalStateException("Could not execute method of the activity", e);
            }
        }

        private void resolveMethod(Context context) {
            while (context != null) {
                try {
                    java.lang.reflect.Method method = context.getClass().getMethod(mMethodName, View.class);
                    if (method != null) {
                        mResolvedMethod = method;
                        mResolvedContext = context;
                        return;
                    }
                } catch (NoSuchMethodException e) {
                    // Try the wrapped context next.
                }
                context = context instanceof android.content.ContextWrapper
                        ? ((android.content.ContextWrapper) context).getBaseContext() : null;
            }
            throw new IllegalStateException("Could not find method " + mMethodName
                    + "(View) in a parent or ancestor Context for android:onClick attribute defined on view "
                    + mHostView.getClass());
        }
    }

    /**
     * returns the context this view was created with.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * returns the id of the view.
     */
    public int getId() {
        return mId;
    }

    /**
     * set the id of the view.
     */
    public void setId(int id) {
        mId = id;
    }

    public View findViewById(int id) {
        if (id == NO_ID) return null;
        return mId == id ? this : null;
    }

    public View findViewWithTag(Object tag) {
        if (tag == null) return null;
        return tag.equals(mTag) ? this : null;
    }

    /** Called after this view and all its children have been inflated from XML. */
    protected void onFinishInflate() {
    }

    public View getRootView() {
        if (mParent instanceof View) {
            return ((View) mParent).getRootView();
        }
        return this;
    }

    private CharSequence mContentDescription;

    public void setContentDescription(CharSequence contentDescription) {
        mContentDescription = contentDescription;
    }

    public CharSequence getContentDescription() {
        return mContentDescription;
    }

    /**
     * returns the left position of the view.
     */
    public int getLeft() {
        return mLeft;
    }

    /**
     * returns the top position of the view.
     */
    public int getTop() {
        return mTop;
    }

    /**
     * returns the right position of the view.
     */
    public int getRight() {
        return mRight;
    }

    /**
     * returns the bottom position of the view.
     */
    public int getBottom() {
        return mBottom;
    }

    /**
     * returns the width of the view.
     */
    public int getWidth() {
        return mRight - mLeft;
    }

    /**
     * returns the height of the view.
     */
    public int getHeight() {
        return mBottom - mTop;
    }

    /**
     * sets the view's layout boundaries.
     *
     * Android's contract: layout() records the bounds and then hands them to
     * onLayout() so a container can position its children. Only assigning the
     * fields (which is what this used to do) means ViewGroup.onLayout never runs,
     * every child keeps its default 0,0,0,0 bounds, and the whole hierarchy draws
     * on top of itself in the top-left corner.
     */
    public void layout(int l, int t, int r, int b) {
        final boolean changed = (l != mLeft) || (t != mTop) || (r != mRight) || (b != mBottom);
        final int oldLeft = mLeft, oldTop = mTop, oldRight = mRight, oldBottom = mBottom;
        final int oldWidth = mRight - mLeft;
        final int oldHeight = mBottom - mTop;
        mLeft = l;
        mTop = t;
        mRight = r;
        mBottom = b;
        mLaidOut = true;
        final int width = r - l;
        final int height = b - t;
        if (width != oldWidth || height != oldHeight) {
            onSizeChanged(width, height, oldWidth, oldHeight);
        }
        mInLayout = true;
        try {
            onLayout(changed, l, t, r, b);
        } finally {
            mInLayout = false;
        }
        if (changed && mLayoutChangeListeners != null) {
            OnLayoutChangeListener[] listeners = mLayoutChangeListeners.toArray(
                    new OnLayoutChangeListener[mLayoutChangeListeners.size()]);
            for (OnLayoutChangeListener listener : listeners) {
                listener.onLayoutChange(this, l, t, r, b, oldLeft, oldTop, oldRight, oldBottom);
            }
        }
    }

    /** Notify subclasses of new dimensions before laying out their children. */
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
    }

    /**
     * Position child views. Leaf views have nothing to do; ViewGroup overrides this.
     */
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
    }

    /** True once layout() has run at least once. */
    public boolean isLaidOut() {
        return mLaidOut;
    }

    /**
     * True while a layout pass is owed to this view.
     *
     * The inverse of isLaidOut() under KuDroid's model: requestLayout() clears mLaidOut and
     * layout() sets it, so "not laid out" and "layout requested" are the same state. Android
     * tracks them separately because it batches passes; here a request is served on the next
     * top-down render.
     *
     * Declared on View rather than only on ViewGroup because ViewParent requires it and
     * ViewGroup inherits it from here.
     */
    public boolean isLayoutRequested() {
        return !mLaidOut;
    }

    /**
     * returns the view's visibility.
     */
    public int getVisibility() {
        return mVisibility;
    }

    /**
     * set the view's visibility.
     */
    public void setVisibility(int visibility) {
        if (mVisibility == visibility) return;
        final int old = mVisibility;
        mVisibility = visibility;
        if ((old == GONE) != (visibility == GONE)) {
            requestLayout();
        }
        invalidate();
        dispatchVisibilityChanged(this, visibility);
        if (mAttachedToWindow) {
            dispatchVisibilityAggregated(isShown());
        }
    }

    /**
     * The container this view sits in, or null at the root.
     *
     * Declared as ViewParent, not ViewGroup, because that is the signature guest code
     * references: {@code View.getParent()Landroid/view/ViewParent;}. Returning ViewGroup
     * made that reference resolve to nothing, so an app assigning the result to a
     * ViewParent got a NoSuchMethodError for a method that plainly existed — it was the
     * RETURN TYPE that did not match. Five of six APKs in the corpus need this one.
     */
    public ViewParent getParent() {
        return mParent;
    }

    /**
     * The parent as a ViewGroup, for callers inside the framework that need to touch child
     * management. Separate from getParent() so that one can keep Android's signature.
     */
    public ViewGroup getParentGroup() {
        return mParent;
    }

    /**
     * set the view's parent.
     */
    public void setParent(ViewGroup parent) {
        mParent = parent;
    }

    /**
     * set up click listener.
     */
    public void setOnClickListener(OnClickListener l) {
        if (!mClickable) mClickable = true;
        mOnClickListener = l;
    }

    public void setOnLongClickListener(OnLongClickListener l) {
        if (!mLongClickable) mLongClickable = true;
        mOnLongClickListener = l;
    }

    public void setOnTouchListener(OnTouchListener l) {
        mOnTouchListener = l;
    }

    public void setOnFocusChangeListener(OnFocusChangeListener l) {
        mOnFocusChangeListener = l;
    }

    public void setOnKeyListener(OnKeyListener l) {
        mOnKeyListener = l;
    }

    public boolean isClickable() {
        return mClickable;
    }

    public void setClickable(boolean clickable) {
        mClickable = clickable;
    }

    public boolean isLongClickable() {
        return mLongClickable;
    }

    public void setLongClickable(boolean longClickable) {
        mLongClickable = longClickable;
    }

    public boolean hasOnClickListeners() {
        return mOnClickListener != null;
    }

    public boolean callOnClick() {
        return performClick();
    }

    public boolean hasFocus() {
        return mHasFocus;
    }

    public boolean hasWindowFocus() {
        return true;
    }

    public boolean isFocused() {
        return mHasFocus;
    }

    /** The view that holds focus. One per process, as there is one window. */
    private static View sFocusedView;

    /** The view that currently holds focus, or null. */
    protected static View getFocusedView() {
        return sFocusedView;
    }

    /** Change focus state and fire callbacks if there is a real change. */
    public void setFocus(boolean hasFocus) {
        if (mHasFocus == hasFocus) return;
        if (hasFocus) {
            // Focus is exclusive: the previous owner loses it before this view gains it.
            final View previous = sFocusedView;
            sFocusedView = this;
            if (previous != null && previous != this) previous.setFocus(false);
        } else if (sFocusedView == this) {
            sFocusedView = null;
        }
        mHasFocus = hasFocus;
        refreshDrawableState();
        onFocusChanged(hasFocus, 0, null);
        if (mOnFocusChangeListener != null) {
            mOnFocusChangeListener.onFocusChange(this, hasFocus);
        }
    }

    /** Called when this view gains or loses focus. */
    protected void onFocusChanged(boolean gainFocus, int direction,
                                  android.graphics.Rect previouslyFocusedRect) {
    }

    public void clearFocus() {
        setFocus(false);
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event == null) return false;
        if (mOnKeyListener != null &&
            mOnKeyListener.onKey(this, event.getKeyCode(), event)) {
            return true;
        }
        return event.dispatch(this, getKeyDispatcherState(), this);
    }

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyLongPress(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyMultiple(int keyCode, int count, KeyEvent event) {
        return false;
    }

    public KeyEvent.DispatcherState getKeyDispatcherState() {
        return null;
    }

    // System UI visibility (deprecated in API 30 but still read by compat code).

    public static final int SYSTEM_UI_FLAG_VISIBLE = 0;
    public static final int SYSTEM_UI_FLAG_LOW_PROFILE = 0x00000001;
    public static final int SYSTEM_UI_FLAG_HIDE_NAVIGATION = 0x00000002;
    public static final int SYSTEM_UI_FLAG_FULLSCREEN = 0x00000004;
    public static final int SYSTEM_UI_FLAG_LAYOUT_STABLE = 0x00000100;
    public static final int SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION = 0x00000200;
    public static final int SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN = 0x00000400;
    public static final int SYSTEM_UI_FLAG_IMMERSIVE = 0x00000800;
    public static final int SYSTEM_UI_FLAG_IMMERSIVE_STICKY = 0x00001000;
    public static final int SYSTEM_UI_FLAG_LIGHT_STATUS_BAR = 0x00002000;
    public static final int SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR = 0x00000010;

    private int mSystemUiVisibility = SYSTEM_UI_FLAG_VISIBLE;
    private OnSystemUiVisibilityChangeListener mOnSystemUiVisibilityChangeListener;

    public int getSystemUiVisibility() {
        return mSystemUiVisibility;
    }

    public void setSystemUiVisibility(int visibility) {
        if (mSystemUiVisibility == visibility) return;
        mSystemUiVisibility = visibility;
        if (mOnSystemUiVisibilityChangeListener != null) {
            mOnSystemUiVisibilityChangeListener.onSystemUiVisibilityChange(visibility);
        }
    }

    public void setOnSystemUiVisibilityChangeListener(
            OnSystemUiVisibilityChangeListener l) {
        mOnSystemUiVisibilityChangeListener = l;
    }

    public boolean isInEditMode() {
        return false;
    }

    // Layout params.
    private ViewGroup.LayoutParams mLayoutParams;

    public ViewGroup.LayoutParams getLayoutParams() {
        return mLayoutParams;
    }

    public void setLayoutParams(ViewGroup.LayoutParams params) {
        if (params == null) throw new NullPointerException("Layout parameters cannot be null");
        mLayoutParams = params;
        requestLayout();
    }

    /** Assign layout parameters without scheduling a layout (the parent does that once). */
    void setLayoutParamsNoRequest(ViewGroup.LayoutParams params) {
        mLayoutParams = params;
    }

    // IME attachment.

    /** Create the InputConnection for an IME, or null when text is not accepted. */
    public android.view.inputmethod.InputConnection onCreateInputConnection(
            android.view.inputmethod.EditorInfo outAttrs) {
        return null;
    }

    /** True when this view wants a soft keyboard. */
    public boolean onCheckIsTextEditor() {
        return false;
    }

    /**
     * Ask for focus.
     *
     * The IME is shown for the focused view, so this is the hook that decides which
     * InputConnection subsequent text goes to.
     */
    public boolean requestFocus() {
        setFocus(true);
        return true;
    }

    public boolean isFocusable() {
        return mFocusable;
    }

    public void setFocusable(boolean focusable) {
        mFocusable = focusable;
    }

    public boolean isFocusableInTouchMode() {
        return mFocusableInTouchMode;
    }

    public void setFocusableInTouchMode(boolean focusable) {
        mFocusableInTouchMode = focusable;
        if (focusable) mFocusable = true;
    }

    /**
     * The token identifying the window this view is in.
     *
     * Apps pass it to InputMethodManager.hideSoftInputFromWindow. There is one window
     * under KuDroid, so a per-view token would be meaningless; the root view stands in
     * for it, which keeps the identity comparison apps make meaningful.
     */
    public android.os.IBinder getWindowToken() {
        return null;
    }

    /**
     * ViewTreeObserver of this view tree (shared with parent if attached).
     */
    public ViewTreeObserver getViewTreeObserver() {
        if (mParent instanceof View) return ((View) mParent).getViewTreeObserver();
        if (mViewTreeObserver == null) mViewTreeObserver = new ViewTreeObserver();
        return mViewTreeObserver;
    }

    public void setAccessibilityDelegate(AccessibilityDelegate delegate) {
        mAccessibilityDelegate = delegate;
    }

    public boolean dispatchTouchEvent(MotionEvent event) {
        if (mVisibility != VISIBLE) return false;
        // OnTouchListener runs BEFORE onTouchEvent and has event consumption —
        // This order is Android's contract, the app relies on it to block clicks.
        if (mOnTouchListener != null && mOnTouchListener.onTouch(this, event)) {
            return true;
        }
        return onTouchEvent(event);
    }

    public boolean onTouchEvent(MotionEvent event) {
        if (event == null) return false;
        final int action = event.getAction();
        if (action == MotionEvent.ACTION_DOWN) {
            mTouchDownTime = android.os.SystemClock.uptimeMillis();
            mLongClickFired = false;
            return mClickable || mLongClickable;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            if (!mLongClickFired && mOnLongClickListener != null && mTouchDownTime > 0 &&
                android.os.SystemClock.uptimeMillis() - mTouchDownTime >= LONG_PRESS_TIMEOUT) {
                mLongClickFired = true;
                return performLongClick();
            }
            return mClickable || mLongClickable;
        }
        if (action == MotionEvent.ACTION_UP) {
            final long held = mTouchDownTime > 0
                    ? android.os.SystemClock.uptimeMillis() - mTouchDownTime : 0;
            mTouchDownTime = 0;
            if (mLongClickFired) return true;
            if (mOnLongClickListener != null && held >= LONG_PRESS_TIMEOUT) {
                return performLongClick();
            }
            if (mOnClickListener != null) {
                performClick();
                return true;
            }
            return mLongClickable;
        }
        return mClickable || mLongClickable;
    }

    /**
     * make one click.
     */
    public boolean performClick() {
        if (mOnClickListener != null) {
            mOnClickListener.onClick(this);
            return true;
        }
        return false;
    }

    public boolean performLongClick() {
        if (mOnLongClickListener != null) {
            return mOnLongClickListener.onLongClick(this);
        }
        return false;
    }

    /**
     * Measure view. Subclasses must call setMeasuredDimension() before returning.
     * The default reports zero size, matching Android's behaviour for a bare View.
     */
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(getDefaultSize(0, widthMeasureSpec),
                             getDefaultSize(0, heightMeasureSpec));
    }

    /**
     * measure view (called by parent).
     */
    public void measure(int widthMeasureSpec, int heightMeasureSpec) {
        onMeasure(widthMeasureSpec, heightMeasureSpec);
    }

    /**
     * Record the size decided by onMeasure. Kept separate from mLeft/mRight because
     * measuring happens before positioning: the parent needs to know how big a child
     * wants to be in order to work out where to put it.
     */
    protected final void setMeasuredDimension(int measuredWidth, int measuredHeight) {
        mMeasuredWidth = measuredWidth;
        mMeasuredHeight = measuredHeight;
    }

    public final int getMeasuredWidth() {
        return mMeasuredWidth;
    }

    public final int getMeasuredHeight() {
        return mMeasuredHeight;
    }

    /**
     * Resolve a measure spec the way Android does: honour an exact or bounded size
     * from the parent, otherwise fall back to the view's own preference.
     */
    public static int getDefaultSize(int size, int measureSpec) {
        final int specMode = MeasureSpec.getMode(measureSpec);
        final int specSize = MeasureSpec.getSize(measureSpec);
        switch (specMode) {
            case MeasureSpec.AT_MOST:
            case MeasureSpec.EXACTLY:
                return specSize;
            default:
                return size;
        }
    }

    /**
     * Packs a size and a constraint mode into one int, as Android does, so a parent
     * can tell a child "you get exactly N" or "at most N" in a single argument.
     */
    public static class MeasureSpec {
        public static final int UNSPECIFIED = 0;
        public static final int EXACTLY = 1;
        public static final int AT_MOST = 2;

        private static final int MODE_SHIFT = 30;
        private static final int MODE_MASK = 0x3 << MODE_SHIFT;

        public static int makeMeasureSpec(int size, int mode) {
            return (size & ~MODE_MASK) | (mode << MODE_SHIFT);
        }

        public static int getMode(int measureSpec) {
            return (measureSpec & MODE_MASK) >>> MODE_SHIFT;
        }

        public static int getSize(int measureSpec) {
            return measureSpec & ~MODE_MASK;
        }
    }

    private int mBackgroundColor = 0;

    /**
     * draw views.
     */
    protected void onDraw(Canvas canvas) {
    }

    protected void onAttachedToWindow() {
    }

    protected void onDetachedFromWindow() {
    }

    /**
     * draw view (called by parent).
     */
    public void draw(Canvas canvas) {
        if (mVisibility != VISIBLE || canvas == null) return;
        if (mBackgroundColor != 0) {
            android.graphics.Paint p = new android.graphics.Paint();
            p.setColor(mBackgroundColor);
            canvas.drawRect(mLeft, mTop, mRight, mBottom, p);
        }
        onDraw(canvas);
    }

    /**
     * returns the display state of the view.
     */
    public boolean isShown() {
        View current = this;
        do {
            if (current.mVisibility != VISIBLE) {
                return false;
            }
            current = current.mParent;
        } while (current != null);
        return true;
    }

    private android.graphics.drawable.Drawable mBackground;

    /**
     * returns the background of the view.
     */
    public android.graphics.drawable.Drawable getBackground() {
        return mBackground;
    }

    /**
     * The modern name for setBackgroundDrawable, and the one apps use.
     *
     * All five real APKs in the corpus reference setBackground; only the deprecated spelling
     * existed. Stored rather than dropped so getBackground() returns what was set — a view
     * that accepts a background and reports null makes any app that reads it back believe it
     * has none.
     */
    public void setBackground(android.graphics.drawable.Drawable background) {
        if (mBackground == background) return;
        if (mBackground != null) {
            mBackground.setCallback(null);
        }
        mBackground = background;
        if (background != null) {
            background.setCallback(this);
            background.setVisible(mVisibility == VISIBLE, false);
            if (background.isStateful()) {
                background.setState(getDrawableState());
            }
            applyBackgroundTint();
        }
        invalidate();
    }

    /**
     * Where this view sits on screen, written into `location` as {x, y}.
     *
     * Walked up the parent chain rather than answered with the view's own left/top, which
     * would only be right for a child of the root. Callers use this to position popups and
     * hit-test against other views, so an answer in the wrong coordinate space puts things
     * visibly in the wrong place.
     *
     * KuDroid draws the guest full-screen with no window offset, so screen and window
     * coordinates coincide and getLocationInWindow gives the same answer.
     */
    public void getLocationOnScreen(int[] location) {
        if (location == null || location.length < 2) return;
        int x = 0;
        int y = 0;
        View v = this;
        while (v != null) {
            x += v.mLeft - v.mScrollX;
            y += v.mTop - v.mScrollY;
            v = v.mParent;
        }
        location[0] = x;
        location[1] = y;
    }

    public void getLocationInWindow(int[] location) {
        getLocationOnScreen(location);
    }

    /**
     * The Resources of this view's Context.
     *
     * Needed by all five corpus APKs: this is how view code reads dimensions and densities
     * without being passed a Context. Falls back to the system Resources when the view has
     * no context, because callers chain straight off it — getResources().getDisplayMetrics()
     * is the common form, and a null here is an NPE one frame later.
     */
    public android.content.res.Resources getResources() {
        return mContext != null ? mContext.getResources()
                                : android.content.res.Resources.getSystem();
    }

    public Display getDisplay() {
        return new Display();
    }

    /**
     * set the background of the view.
     */
    public void setBackgroundColor(int color) {
        mBackgroundColor = color;
        invalidate();
    }

    /**
     * set the view's background drawable.
     */
    /** Deprecated spelling; kept because older code calls it. Same storage as setBackground. */
    public void setBackgroundDrawable(android.graphics.drawable.Drawable background) {
        setBackground(background);
    }

    /**
     * sets the view's padding.
     */
    public void setPadding(int left, int top, int right, int bottom) {
        final boolean changed = mPaddingLeft != left || mPaddingTop != top
                || mPaddingRight != right || mPaddingBottom != bottom;
        mPaddingLeft = left;
        mPaddingTop = top;
        mPaddingRight = right;
        mPaddingBottom = bottom;
        if (changed) requestLayout();
    }

    public void setPaddingRelative(int start, int top, int end, int bottom) {
        setPadding(start, top, end, bottom);
    }

    public int getPaddingLeft() { return mPaddingLeft; }
    public int getPaddingTop() { return mPaddingTop; }
    public int getPaddingRight() { return mPaddingRight; }
    public int getPaddingBottom() { return mPaddingBottom; }
    public int getPaddingStart() { return mPaddingLeft; }
    public int getPaddingEnd() { return mPaddingRight; }

    /**
     * invalidate the view.
     *
     * Redraw is driven from the top by Activity.renderViewHierarchy(), so this walks
     * up to the root and asks it to repaint rather than tracking dirty regions.
     */
    public void invalidate() {
        View root = this;
        while (root.mParent != null) {
            root = root.mParent;
        }
        if (root != this) {
            root.invalidate();
        }
    }

    /**
     * Ask for another measure + layout pass. Same top-down model as invalidate().
     */
    public void requestLayout() {
        mLaidOut = false;
        if (mParent != null) {
            mParent.requestLayout();
        }
    }

    private synchronized android.os.Handler getRunQueue() {
        android.os.Looper mainLooper = android.os.Looper.getMainLooper();
        if (mainLooper == null) return null;
        if (mRunQueue == null || mRunQueue.getLooper() != mainLooper) {
            mRunQueue = new android.os.Handler(mainLooper);
        }
        return mRunQueue;
    }

    /**
     * Enqueue work for the UI thread. Posted actions never run inline.
     */
    public boolean post(Runnable action) {
        if (action == null) return false;
        android.os.Handler handler = getRunQueue();
        return handler != null && handler.post(action);
    }

    /** Enqueue work for the UI thread after the requested delay. */
    public boolean postDelayed(Runnable action, long delayMillis) {
        if (action == null) return false;
        android.os.Handler handler = getRunQueue();
        return handler != null && handler.postDelayed(action, delayMillis);
    }

    /** Remove queued instances of a Runnable posted through this View. */
    public boolean removeCallbacks(Runnable action) {
        if (action == null) return false;
        android.os.Handler handler = getRunQueue();
        if (handler != null) handler.removeCallbacks(action);
        return true;
    }

    public final void cancelPendingInputEvents() { dispatchCancelPendingInputEvents(); }

    protected void dispatchCancelPendingInputEvents() { onCancelPendingInputEvents(); }

    public void onCancelPendingInputEvents() {
        mTouchDownTime = 0;
        mLongClickFired = false;
    }

    // Tags.
    private Object mTag;
    private android.util.SparseArray<Object> mKeyedTags;

    public Object getTag() {
        return mTag;
    }

    public void setTag(Object tag) {
        mTag = tag;
    }

    public Object getTag(int key) {
        return mKeyedTags != null ? mKeyedTags.get(key) : null;
    }

    /**
     * Attach data under an integer key.
     *
     * Android requires the key to be an application-declared resource id, and throws
     * IllegalArgumentException otherwise, to stop two libraries colliding on the same
     * arbitrary constant. KuDroid has no resource ids to validate against, so the key
     * is accepted as given: rejecting valid-but-unverifiable keys would break the very
     * libraries this exists for.
     */
    public void setTag(int key, Object tag) {
        if (mKeyedTags == null) mKeyedTags = new android.util.SparseArray<Object>();
        mKeyedTags.put(key, tag);
    }

    /**
     * Generate a view id that cannot collide with an aapt-assigned one.
     *
     * Used by code that creates views programmatically and needs an id for them —
     * ConstraintLayout chains, fragment containers, anything that later calls
     * findViewById on a view it built itself.
     *
     * The range matters and is why this cannot be a naive counter: aapt allocates
     * resource ids from 0x7f000000 upwards, so generated ids have to stay below that
     * to avoid ever matching a real R.id constant. AOSP uses 1..0x00FFFFFF and wraps;
     * copying that keeps the guarantee.
     */
    private static final java.util.concurrent.atomic.AtomicInteger sNextGeneratedId =
            new java.util.concurrent.atomic.AtomicInteger(1);

    public static int generateViewId() {
        for (;;) {
            final int result = sNextGeneratedId.get();
            int newValue = result + 1;
            if (newValue > 0x00FFFFFF) newValue = 1; // roll over, skipping 0
            if (sNextGeneratedId.compareAndSet(result, newValue)) return result;
        }
    }

    // Window insets.

    private OnApplyWindowInsetsListener mOnApplyWindowInsetsListener;
    private boolean mFitsSystemWindows = false;

    /** Install a listener that consumes window insets before this view does. */
    public void setOnApplyWindowInsetsListener(OnApplyWindowInsetsListener listener) {
        mOnApplyWindowInsetsListener = listener;
    }

    public OnApplyWindowInsetsListener getOnApplyWindowInsetsListener() {
        return mOnApplyWindowInsetsListener;
    }

    public void requestApplyInsets() {
        requestFitSystemWindows();
    }

    public void requestFitSystemWindows() {
        if (mParent instanceof View) {
            ((View) mParent).requestFitSystemWindows();
        }
    }

    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        return insets;
    }

    public WindowInsets dispatchApplyWindowInsets(WindowInsets insets) {
        if (mOnApplyWindowInsetsListener != null) {
            WindowInsets result = mOnApplyWindowInsetsListener.onApplyWindowInsets(this, insets);
            return result != null ? result : insets;
        }
        return onApplyWindowInsets(insets);
    }

    public WindowInsets getRootWindowInsets() {
        return null;
    }

    public WindowInsets computeSystemWindowInsets(WindowInsets in, android.graphics.Rect outLocalInsets) {
        return in;
    }

    public boolean fitsSystemWindows() {
        return mFitsSystemWindows;
    }

    public void setFitsSystemWindows(boolean fitSystemWindows) {
        mFitsSystemWindows = fitSystemWindows;
    }

    public boolean getFitsSystemWindows() {
        return mFitsSystemWindows;
    }

    private boolean mKeepScreenOn = false;
    private static native void setKeepScreenOnNative(boolean keepOn);

    public void setKeepScreenOn(boolean keepScreenOn) {
        mKeepScreenOn = keepScreenOn;
        try {
            setKeepScreenOnNative(keepScreenOn);
        } catch (Throwable ignored) {}
    }

    public boolean getKeepScreenOn() {
        return mKeepScreenOn;
    }

    /** Base class for the state a view saves; carries the superclass' state. */
    public static class BaseSavedState extends AbsSavedState {
        public BaseSavedState(android.os.Parcel source) {
            this(source, null);
        }

        public BaseSavedState(android.os.Parcel source, ClassLoader loader) {
            super(source, loader);
        }

        public BaseSavedState(android.os.Parcelable superState) {
            super(superState);
        }

        public static final android.os.Parcelable.Creator<BaseSavedState> CREATOR =
                new android.os.Parcelable.Creator<BaseSavedState>() {
                    public BaseSavedState createFromParcel(android.os.Parcel in) {
                        return new BaseSavedState(in);
                    }

                    public BaseSavedState[] newArray(int size) {
                        return new BaseSavedState[size];
                    }
                };
    }

    public void scrollTo(int x, int y) {
        if (mScrollX != x || mScrollY != y) {
            final int oldX = mScrollX;
            final int oldY = mScrollY;
            mScrollX = x;
            mScrollY = y;
            onScrollChanged(mScrollX, mScrollY, oldX, oldY);
            invalidate();
        }
    }
    public void scrollBy(int x, int y) {
        scrollTo(mScrollX + x, mScrollY + y);
    }

    public interface OnSystemUiVisibilityChangeListener {
        void onSystemUiVisibilityChange(int visibility);
    }

    public void saveAttributeDataForStyleable(Context context, int[] styleable,
            android.util.AttributeSet set, android.content.res.TypedArray attrs,
            int defStyleAttr, int defStyleRes) {
    }

    // State flags. They are stored and reported; dispatching them to drawables comes with
    // drawable support.

    public android.graphics.drawable.Drawable getForeground() { return mForeground; }
    public void setForeground(android.graphics.drawable.Drawable foreground) {
        if (mForeground == foreground) return;
        if (mForeground != null) {
            mForeground.setCallback(null);
        }
        mForeground = foreground;
        if (foreground != null) {
            foreground.setCallback(this);
            foreground.setVisible(mVisibility == VISIBLE, false);
            if (foreground.isStateful()) {
                foreground.setState(getDrawableState());
            }
            applyForegroundTint();
        }
        invalidate();
    }

    public boolean isEnabled() { return mEnabled; }
    public void setEnabled(boolean enabled) {
        if (mEnabled == enabled) return;
        mEnabled = enabled;
        refreshDrawableState();
        invalidate();
        if (!enabled) cancelPendingInputEvents();
    }
    public boolean isSelected() { return mSelected; }
    public void setSelected(boolean selected) {
        if (mSelected == selected) return;
        mSelected = selected;
        invalidate();
        refreshDrawableState();
        dispatchSetSelected(selected);
    }

    protected void dispatchSetSelected(boolean selected) {
    }
    public boolean isPressed() { return mPressed; }
    public void setPressed(boolean pressed) {
        if (mPressed == pressed) return;
        mPressed = pressed;
        refreshDrawableState();
        invalidate();
        dispatchSetPressed(pressed);
    }

    protected void dispatchSetPressed(boolean pressed) {
    }
    public boolean isActivated() { return mActivated; }
    public void setActivated(boolean activated) {
        if (mActivated == activated) return;
        mActivated = activated;
        invalidate();
        refreshDrawableState();
        dispatchSetActivated(activated);
    }

    protected void dispatchSetActivated(boolean activated) {
    }

    // Property transforms are stored for getters and animators; drawing does not apply them.
    public float getAlpha() { return mAlpha; }
    public void setAlpha(float alpha) { mAlpha = alpha; invalidate(); }
    public float getTranslationX() { return mTranslationX; }
    public void setTranslationX(float v) { mTranslationX = v; invalidate(); }
    public float getTranslationY() { return mTranslationY; }
    public void setTranslationY(float v) { mTranslationY = v; invalidate(); }
    public float getTranslationZ() { return mTranslationZ; }
    public void setTranslationZ(float v) { mTranslationZ = v; invalidate(); }
    public float getElevation() { return mElevation; }
    public void setElevation(float v) { mElevation = v; }
    public float getScaleX() { return mScaleX; }
    public void setScaleX(float v) { mScaleX = v; invalidate(); }
    public float getScaleY() { return mScaleY; }
    public void setScaleY(float v) { mScaleY = v; invalidate(); }
    public float getRotation() { return mRotation; }
    public void setRotation(float v) { mRotation = v; invalidate(); }
    public float getPivotX() { return mPivotSet ? mPivotX : getWidth() / 2f; }
    public void setPivotX(float v) { mPivotX = v; mPivotSet = true; }
    public float getPivotY() { return mPivotSet ? mPivotY : getHeight() / 2f; }
    public void setPivotY(float v) { mPivotY = v; mPivotSet = true; }
    public float getX() { return mLeft + mTranslationX; }
    public float getY() { return mTop + mTranslationY; }

    // Measurement helpers.

    public static final int MEASURED_SIZE_MASK = 0x00ffffff;
    public static final int MEASURED_STATE_MASK = 0xff000000;
    public static final int MEASURED_HEIGHT_STATE_SHIFT = 16;
    public static final int MEASURED_STATE_TOO_SMALL = 0x01000000;

    public int getMinimumWidth() { return mMinWidth; }
    public int getMinimumHeight() { return mMinHeight; }
    public void setMinimumWidth(int minWidth) { mMinWidth = minWidth; requestLayout(); }
    public void setMinimumHeight(int minHeight) { mMinHeight = minHeight; requestLayout(); }

    protected int getSuggestedMinimumWidth() {
        return mBackground == null ? mMinWidth : Math.max(mMinWidth, mBackground.getMinimumWidth());
    }

    protected int getSuggestedMinimumHeight() {
        return mBackground == null ? mMinHeight : Math.max(mMinHeight, mBackground.getMinimumHeight());
    }

    public static int resolveSize(int size, int measureSpec) {
        return resolveSizeAndState(size, measureSpec, 0) & MEASURED_SIZE_MASK;
    }

    public static int resolveSizeAndState(int size, int measureSpec, int childMeasuredState) {
        final int specMode = MeasureSpec.getMode(measureSpec);
        final int specSize = MeasureSpec.getSize(measureSpec);
        final int result;
        switch (specMode) {
            case MeasureSpec.AT_MOST:
                if (specSize < size) {
                    result = specSize | MEASURED_STATE_TOO_SMALL;
                } else {
                    result = size;
                }
                break;
            case MeasureSpec.EXACTLY:
                result = specSize;
                break;
            case MeasureSpec.UNSPECIFIED:
            default:
                result = size;
        }
        return result | (childMeasuredState & MEASURED_STATE_MASK);
    }

    public static int combineMeasuredStates(int curState, int newState) {
        return curState | newState;
    }

    public final int getMeasuredState() {
        return (mMeasuredWidth & MEASURED_STATE_MASK)
                | ((mMeasuredHeight >> MEASURED_HEIGHT_STATE_SHIFT)
                & (MEASURED_STATE_MASK >> MEASURED_HEIGHT_STATE_SHIFT));
    }

    /** Mark this view as needing layout without asking ancestors. */
    public void forceLayout() {
        mLaidOut = false;
    }

    // Posting.

    public void postInvalidate() {
        post(new Runnable() { public void run() { invalidate(); } });
    }

    public void postOnAnimation(Runnable action) {
        post(action);
    }

    public void postOnAnimationDelayed(Runnable action, long delayMillis) {
        postDelayed(action, delayMillis);
    }

    // Layout direction: left-to-right only.
    public static final int LAYOUT_DIRECTION_LTR = 0;
    public static final int LAYOUT_DIRECTION_RTL = 1;
    public int getLayoutDirection() { return LAYOUT_DIRECTION_LTR; }
    public boolean isLayoutRtl() { return false; }

    public void setBackgroundResource(int resid) {
        setBackground(resid == 0 ? null : getContext().getResources().getDrawable(resid, getContext().getTheme()));
    }

    // ------------------------------------------------------------------------------------
    // Attach state, visibility and window callbacks.

    private boolean mAttachedToWindow;
    private int mWindowAttachCount;
    private java.util.concurrent.CopyOnWriteArrayList<OnAttachStateChangeListener>
            mAttachStateListeners;
    private boolean mTemporarilyDetached;

    public boolean isAttachedToWindow() {
        return mAttachedToWindow;
    }

    protected int getWindowAttachCount() {
        return mWindowAttachCount;
    }

    public void addOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        if (listener == null) return;
        if (mAttachStateListeners == null) {
            mAttachStateListeners =
                    new java.util.concurrent.CopyOnWriteArrayList<OnAttachStateChangeListener>();
        }
        mAttachStateListeners.add(listener);
    }

    public void removeOnAttachStateChangeListener(OnAttachStateChangeListener listener) {
        if (mAttachStateListeners != null) mAttachStateListeners.remove(listener);
    }

    /** Called when the hierarchy this view belongs to becomes part of a window. */
    void dispatchAttachedToWindow() {
        if (mAttachedToWindow) return;
        mAttachedToWindow = true;
        mWindowAttachCount++;
        onAttachedToWindow();
        if (mAttachStateListeners != null) {
            for (OnAttachStateChangeListener l : mAttachStateListeners) {
                l.onViewAttachedToWindow(this);
            }
        }
        refreshDrawableState();
        onVisibilityAggregated(isShown());
    }

    void dispatchDetachedFromWindow() {
        if (!mAttachedToWindow) return;
        onDetachedFromWindow();
        if (mAttachStateListeners != null) {
            for (OnAttachStateChangeListener l : mAttachStateListeners) {
                l.onViewDetachedFromWindow(this);
            }
        }
        mAttachedToWindow = false;
        onVisibilityAggregated(false);
    }

    /** Called when the visibility of this view or any ancestor changed. */
    public void onVisibilityAggregated(boolean isVisible) {
        if (mBackground != null) mBackground.setVisible(isVisible, false);
        if (mForeground != null) mForeground.setVisible(isVisible, false);
    }

    boolean dispatchVisibilityAggregated(boolean isVisible) {
        final boolean thisVisible = getVisibility() == VISIBLE;
        if (thisVisible || !isVisible) {
            onVisibilityAggregated(isVisible);
        }
        return thisVisible && isVisible;
    }

    protected void dispatchVisibilityChanged(View changedView, int visibility) {
        onVisibilityChanged(changedView, visibility);
    }

    protected void onVisibilityChanged(View changedView, int visibility) {
    }

    public void dispatchWindowVisibilityChanged(int visibility) {
        onWindowVisibilityChanged(visibility);
    }

    protected void onWindowVisibilityChanged(int visibility) {
    }

    public int getWindowVisibility() {
        return VISIBLE;
    }

    public void dispatchWindowFocusChanged(boolean hasFocus) {
        onWindowFocusChanged(hasFocus);
    }

    public void onWindowFocusChanged(boolean hasWindowFocus) {
    }

    public void dispatchConfigurationChanged(android.content.res.Configuration newConfig) {
        onConfigurationChanged(newConfig);
    }

    protected void onConfigurationChanged(android.content.res.Configuration newConfig) {
    }

    public void dispatchDisplayHint(int hint) {
        onDisplayHint(hint);
    }

    protected void onDisplayHint(int hint) {
    }

    public void dispatchSystemUiVisibilityChanged(int visibility) {
        if (mOnSystemUiVisibilityChangeListener != null) {
            mOnSystemUiVisibilityChangeListener.onSystemUiVisibilityChange(
                    visibility & ~SYSTEM_UI_FLAG_LOW_PROFILE);
        }
    }

    public void dispatchWindowSystemUiVisiblityChanged(int visible) {
        onWindowSystemUiVisibilityChanged(visible);
    }

    public void onWindowSystemUiVisibilityChanged(int visible) {
    }

    public int getWindowSystemUiVisibility() {
        return 0;
    }

    public void onScreenStateChanged(int screenState) {
    }

    public void onRtlPropertiesChanged(int layoutDirection) {
    }

    public android.os.IBinder getApplicationWindowToken() {
        return null;
    }

    public android.os.Handler getHandler() {
        return getRunQueue();
    }

    public boolean isInTouchMode() {
        return true;
    }

    public boolean isHardwareAccelerated() {
        return false;
    }

    public void dispatchStartTemporaryDetach() {
        mTemporarilyDetached = true;
        onStartTemporaryDetach();
    }

    public void dispatchFinishTemporaryDetach() {
        mTemporarilyDetached = false;
        onFinishTemporaryDetach();
    }

    public void onStartTemporaryDetach() {
    }

    public void onFinishTemporaryDetach() {
    }

    public boolean isTemporarilyDetached() {
        return mTemporarilyDetached;
    }

    // ------------------------------------------------------------------------------------
    // View flags that only need to be remembered.

    private boolean mWillNotDraw;
    private boolean mWillNotCacheDrawing;
    private boolean mSaveEnabled = true;
    private boolean mSaveFromParentEnabled = true;
    private boolean mDuplicateParentState;
    private boolean mSoundEffectsEnabled = true;
    private boolean mHapticFeedbackEnabled = true;
    private boolean mHasTransientState;
    private boolean mContextClickable;
    private boolean mHovered;
    private boolean mScreenReaderFocusable;
    private boolean mForceDarkAllowed = true;
    private boolean mDefaultFocusHighlightEnabled = true;
    private boolean mFocusedByDefault;
    private boolean mKeyboardNavigationCluster;
    private boolean mRevealOnFocusHint = true;
    private boolean mFilterTouchesWhenObscured;
    private boolean mScrollContainer;
    private boolean mHasOverlappingRendering = true;
    private boolean mDrawingCacheEnabled;
    private boolean mInLayout;
    private int mLayerType = LAYER_TYPE_NONE;
    private int mDrawingCacheQuality;
    private int mDrawingCacheBackgroundColor;
    private CharSequence mTooltipText;
    private String mTransitionName;
    private float mTransitionAlpha = 1f;
    private int mLabelFor = NO_ID;
    private TouchDelegate mTouchDelegate;
    private android.graphics.Rect mClipBounds;
    private boolean mClipToOutline;
    private ViewOutlineProvider mOutlineProvider = ViewOutlineProvider.BACKGROUND;
    private android.animation.StateListAnimator mStateListAnimator;
    private android.graphics.drawable.Drawable mScrollIndicatorDrawable;

    public static final int LAYER_TYPE_NONE = 0;
    public static final int LAYER_TYPE_SOFTWARE = 1;
    public static final int LAYER_TYPE_HARDWARE = 2;

    public static final int DRAWING_CACHE_QUALITY_LOW = 0x00080000;
    public static final int DRAWING_CACHE_QUALITY_HIGH = 0x00100000;
    public static final int DRAWING_CACHE_QUALITY_AUTO = 0;

    public static final int OVER_SCROLL_ALWAYS = 0;
    public static final int OVER_SCROLL_IF_CONTENT_SCROLLS = 1;
    public static final int OVER_SCROLL_NEVER = 2;

    public static final int SCROLLBARS_INSIDE_OVERLAY = 0;
    public static final int SCROLLBARS_INSIDE_INSET = 0x01000000;
    public static final int SCROLLBARS_OUTSIDE_OVERLAY = 0x02000000;
    public static final int SCROLLBARS_OUTSIDE_INSET = 0x03000000;

    public static final int SCROLLBAR_POSITION_DEFAULT = 0;
    public static final int SCROLLBAR_POSITION_LEFT = 1;
    public static final int SCROLLBAR_POSITION_RIGHT = 2;

    public static final int SCROLL_INDICATOR_TOP = 0x0100;
    public static final int SCROLL_INDICATOR_BOTTOM = 0x0200;
    public static final int SCROLL_INDICATOR_LEFT = 0x0400;
    public static final int SCROLL_INDICATOR_RIGHT = 0x0800;
    public static final int SCROLL_INDICATOR_START = 0x1000;
    public static final int SCROLL_INDICATOR_END = 0x2000;

    public static final int FOCUS_BACKWARD = 0x00000001;
    public static final int FOCUS_FORWARD = 0x00000002;
    public static final int FOCUS_LEFT = 0x00000011;
    public static final int FOCUS_UP = 0x00000021;
    public static final int FOCUS_RIGHT = 0x00000042;
    public static final int FOCUS_DOWN = 0x00000082;
    public static final int FOCUSABLES_ALL = 0x00000000;
    public static final int FOCUSABLES_TOUCH_MODE = 0x00000001;
    public static final int NOT_FOCUSABLE = 0x00000000;
    public static final int FOCUSABLE = 0x00000001;
    public static final int FOCUSABLE_AUTO = 0x00000010;

    public static final int IMPORTANT_FOR_ACCESSIBILITY_AUTO = 0x00000000;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_YES = 0x00000001;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_NO = 0x00000002;
    public static final int IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS = 0x00000004;
    public static final int IMPORTANT_FOR_AUTOFILL_AUTO = 0x0;
    public static final int IMPORTANT_FOR_AUTOFILL_YES = 0x1;
    public static final int IMPORTANT_FOR_AUTOFILL_NO = 0x2;
    public static final int IMPORTANT_FOR_AUTOFILL_YES_EXCLUDE_DESCENDANTS = 0x4;
    public static final int IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS = 0x8;
    public static final int ACCESSIBILITY_LIVE_REGION_NONE = 0x00000000;
    public static final int ACCESSIBILITY_LIVE_REGION_POLITE = 0x00000001;
    public static final int ACCESSIBILITY_LIVE_REGION_ASSERTIVE = 0x00000002;

    public static final int TEXT_DIRECTION_INHERIT = 0;
    public static final int TEXT_DIRECTION_FIRST_STRONG = 1;
    public static final int TEXT_DIRECTION_ANY_RTL = 2;
    public static final int TEXT_DIRECTION_LTR = 3;
    public static final int TEXT_DIRECTION_RTL = 4;
    public static final int TEXT_DIRECTION_LOCALE = 5;
    public static final int TEXT_DIRECTION_FIRST_STRONG_LTR = 6;
    public static final int TEXT_DIRECTION_FIRST_STRONG_RTL = 7;
    public static final int TEXT_ALIGNMENT_INHERIT = 0;
    public static final int TEXT_ALIGNMENT_GRAVITY = 1;
    public static final int TEXT_ALIGNMENT_TEXT_START = 2;
    public static final int TEXT_ALIGNMENT_TEXT_END = 3;
    public static final int TEXT_ALIGNMENT_CENTER = 4;
    public static final int TEXT_ALIGNMENT_VIEW_START = 5;
    public static final int TEXT_ALIGNMENT_VIEW_END = 6;
    public static final int LAYOUT_DIRECTION_INHERIT = 2;
    public static final int LAYOUT_DIRECTION_LOCALE = 3;

    public static final int SOUND_EFFECTS_ENABLED = 0x08000000;
    public static final int HAPTIC_FEEDBACK_ENABLED = 0x10000000;

    public void setWillNotDraw(boolean willNotDraw) {
        mWillNotDraw = willNotDraw;
    }

    public boolean willNotDraw() {
        return mWillNotDraw;
    }

    public void setWillNotCacheDrawing(boolean willNotCacheDrawing) {
        mWillNotCacheDrawing = willNotCacheDrawing;
    }

    public boolean willNotCacheDrawing() {
        return mWillNotCacheDrawing;
    }

    public boolean isSaveEnabled() {
        return mSaveEnabled;
    }

    public void setSaveEnabled(boolean enabled) {
        mSaveEnabled = enabled;
    }

    public boolean isSaveFromParentEnabled() {
        return mSaveFromParentEnabled;
    }

    public void setSaveFromParentEnabled(boolean enabled) {
        mSaveFromParentEnabled = enabled;
    }

    public boolean isDuplicateParentStateEnabled() {
        return mDuplicateParentState;
    }

    public void setDuplicateParentStateEnabled(boolean enabled) {
        mDuplicateParentState = enabled;
    }

    public boolean isSoundEffectsEnabled() {
        return mSoundEffectsEnabled;
    }

    public void setSoundEffectsEnabled(boolean soundEffectsEnabled) {
        mSoundEffectsEnabled = soundEffectsEnabled;
    }

    public boolean isHapticFeedbackEnabled() {
        return mHapticFeedbackEnabled;
    }

    public void setHapticFeedbackEnabled(boolean hapticFeedbackEnabled) {
        mHapticFeedbackEnabled = hapticFeedbackEnabled;
    }

    public void playSoundEffect(int soundConstant) {
    }

    public boolean performHapticFeedback(int feedbackConstant) {
        return false;
    }

    public boolean performHapticFeedback(int feedbackConstant, int flags) {
        return false;
    }

    public boolean hasTransientState() {
        return mHasTransientState;
    }

    public void setHasTransientState(boolean hasTransientState) {
        if (mHasTransientState == hasTransientState) return;
        mHasTransientState = hasTransientState;
        if (mParent != null) {
            mParent.childHasTransientStateChanged(this, hasTransientState);
        }
    }

    public boolean isContextClickable() {
        return mContextClickable;
    }

    public void setContextClickable(boolean contextClickable) {
        mContextClickable = contextClickable;
    }

    public boolean isHovered() {
        return mHovered;
    }

    public void setHovered(boolean hovered) {
        if (mHovered == hovered) return;
        mHovered = hovered;
        refreshDrawableState();
        onHoverChanged(hovered);
    }

    public void onHoverChanged(boolean hovered) {
    }

    public boolean isScreenReaderFocusable() {
        return mScreenReaderFocusable;
    }

    public void setScreenReaderFocusable(boolean screenReaderFocusable) {
        mScreenReaderFocusable = screenReaderFocusable;
    }

    public boolean isForceDarkAllowed() {
        return mForceDarkAllowed;
    }

    public void setForceDarkAllowed(boolean allow) {
        mForceDarkAllowed = allow;
    }

    public boolean getDefaultFocusHighlightEnabled() {
        return mDefaultFocusHighlightEnabled;
    }

    public void setDefaultFocusHighlightEnabled(boolean enabled) {
        mDefaultFocusHighlightEnabled = enabled;
    }

    public boolean isFocusedByDefault() {
        return mFocusedByDefault;
    }

    public void setFocusedByDefault(boolean isFocusedByDefault) {
        mFocusedByDefault = isFocusedByDefault;
    }

    public boolean isKeyboardNavigationCluster() {
        return mKeyboardNavigationCluster;
    }

    public void setKeyboardNavigationCluster(boolean isCluster) {
        mKeyboardNavigationCluster = isCluster;
    }

    public boolean getRevealOnFocusHint() {
        return mRevealOnFocusHint;
    }

    public void setRevealOnFocusHint(boolean revealOnFocus) {
        mRevealOnFocusHint = revealOnFocus;
    }

    public boolean getFilterTouchesWhenObscured() {
        return mFilterTouchesWhenObscured;
    }

    public void setFilterTouchesWhenObscured(boolean enabled) {
        mFilterTouchesWhenObscured = enabled;
    }

    public boolean onFilterTouchEventForSecurity(MotionEvent event) {
        return true;
    }

    public boolean isScrollContainer() {
        return mScrollContainer;
    }

    public void setScrollContainer(boolean isScrollContainer) {
        mScrollContainer = isScrollContainer;
    }

    public boolean hasOverlappingRendering() {
        return mHasOverlappingRendering;
    }

    public boolean getHasOverlappingRendering() {
        return hasOverlappingRendering();
    }

    public void forceHasOverlappingRendering(boolean hasOverlappingRendering) {
        mHasOverlappingRendering = hasOverlappingRendering;
    }

    public boolean isOpaque() {
        return mBackground != null
                && mBackground.getOpacity() == android.graphics.PixelFormat.OPAQUE;
    }

    public boolean isInLayout() {
        return mInLayout;
    }

    public boolean isDirty() {
        return false;
    }

    public int getLayerType() {
        return mLayerType;
    }

    public void setLayerType(int layerType, android.graphics.Paint paint) {
        if (layerType < LAYER_TYPE_NONE || layerType > LAYER_TYPE_HARDWARE) {
            throw new IllegalArgumentException("Layer type can only be one of: LAYER_TYPE_NONE, "
                    + "LAYER_TYPE_SOFTWARE or LAYER_TYPE_HARDWARE");
        }
        mLayerType = layerType;
        invalidate();
    }

    public void setLayerPaint(android.graphics.Paint paint) {
        invalidate();
    }

    public void buildLayer() {
    }

    public boolean isDrawingCacheEnabled() {
        return mDrawingCacheEnabled;
    }

    public void setDrawingCacheEnabled(boolean enabled) {
        mDrawingCacheEnabled = enabled;
    }

    public android.graphics.Bitmap getDrawingCache() {
        return null;
    }

    public android.graphics.Bitmap getDrawingCache(boolean autoScale) {
        return null;
    }

    public void buildDrawingCache() {
    }

    public void buildDrawingCache(boolean autoScale) {
    }

    public void destroyDrawingCache() {
    }

    public int getDrawingCacheQuality() {
        return mDrawingCacheQuality;
    }

    public void setDrawingCacheQuality(int quality) {
        mDrawingCacheQuality = quality;
    }

    public int getDrawingCacheBackgroundColor() {
        return mDrawingCacheBackgroundColor;
    }

    public void setDrawingCacheBackgroundColor(int color) {
        mDrawingCacheBackgroundColor = color;
    }

    public CharSequence getTooltipText() {
        return mTooltipText;
    }

    public void setTooltipText(CharSequence tooltipText) {
        mTooltipText = tooltipText;
    }

    public String getTransitionName() {
        return mTransitionName;
    }

    public void setTransitionName(String transitionName) {
        mTransitionName = transitionName;
    }

    public float getTransitionAlpha() {
        return mTransitionAlpha;
    }

    public void setTransitionAlpha(float alpha) {
        mTransitionAlpha = alpha;
        invalidate();
    }

    public void setTransitionVisibility(int visibility) {
        mVisibility = visibility;
    }

    public int getLabelFor() {
        return mLabelFor;
    }

    public void setLabelFor(int id) {
        mLabelFor = id;
    }

    public TouchDelegate getTouchDelegate() {
        return mTouchDelegate;
    }

    public void setTouchDelegate(TouchDelegate delegate) {
        mTouchDelegate = delegate;
    }

    public android.graphics.Rect getClipBounds() {
        return mClipBounds != null ? new android.graphics.Rect(mClipBounds) : null;
    }

    public boolean getClipBounds(android.graphics.Rect outRect) {
        if (mClipBounds != null) {
            outRect.set(mClipBounds);
            return true;
        }
        return false;
    }

    public void setClipBounds(android.graphics.Rect clipBounds) {
        mClipBounds = clipBounds != null ? new android.graphics.Rect(clipBounds) : null;
        invalidate();
    }

    public boolean getClipToOutline() {
        return mClipToOutline;
    }

    public void setClipToOutline(boolean clipToOutline) {
        mClipToOutline = clipToOutline;
    }

    public ViewOutlineProvider getOutlineProvider() {
        return mOutlineProvider;
    }

    public void setOutlineProvider(ViewOutlineProvider provider) {
        mOutlineProvider = provider;
    }

    public void invalidateOutline() {
    }

    public void setOutlineAmbientShadowColor(int color) {
    }

    public void setOutlineSpotShadowColor(int color) {
    }

    public android.animation.StateListAnimator getStateListAnimator() {
        return mStateListAnimator;
    }

    public void setStateListAnimator(android.animation.StateListAnimator stateListAnimator) {
        if (mStateListAnimator == stateListAnimator) return;
        if (mStateListAnimator != null) {
            mStateListAnimator.setTarget(null);
        }
        mStateListAnimator = stateListAnimator;
        if (stateListAnimator != null) {
            stateListAnimator.setTarget(this);
            if (mAttachedToWindow) {
                stateListAnimator.setState(getDrawableState());
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // Layout bounds, position and transforms.

    public void setLeft(int left) {
        setLeftTopRightBottom(left, mTop, mRight, mBottom);
    }

    public void setTop(int top) {
        setLeftTopRightBottom(mLeft, top, mRight, mBottom);
    }

    public void setRight(int right) {
        setLeftTopRightBottom(mLeft, mTop, right, mBottom);
    }

    public void setBottom(int bottom) {
        setLeftTopRightBottom(mLeft, mTop, mRight, bottom);
    }

    /** Moves the view's edges without a layout pass; used by animators. */
    public final void setLeftTopRightBottom(int left, int top, int right, int bottom) {
        final int oldWidth = mRight - mLeft;
        final int oldHeight = mBottom - mTop;
        mLeft = left;
        mTop = top;
        mRight = right;
        mBottom = bottom;
        final int newWidth = right - left;
        final int newHeight = bottom - top;
        if (newWidth != oldWidth || newHeight != oldHeight) {
            onSizeChanged(newWidth, newHeight, oldWidth, oldHeight);
        }
        invalidate();
    }

    public void offsetLeftAndRight(int offset) {
        if (offset != 0) {
            mLeft += offset;
            mRight += offset;
            invalidate();
        }
    }

    public void offsetTopAndBottom(int offset) {
        if (offset != 0) {
            mTop += offset;
            mBottom += offset;
            invalidate();
        }
    }

    public void setX(float x) {
        setTranslationX(x - mLeft);
    }

    public void setY(float y) {
        setTranslationY(y - mTop);
    }

    public float getZ() {
        return getElevation() + getTranslationZ();
    }

    public void setZ(float z) {
        setTranslationZ(z - getElevation());
    }

    private float mRotationX;
    private float mRotationY;
    private float mCameraDistance;
    private android.graphics.Matrix mAnimationMatrix;

    public float getRotationX() {
        return mRotationX;
    }

    public void setRotationX(float rotationX) {
        mRotationX = rotationX;
        invalidate();
    }

    public float getRotationY() {
        return mRotationY;
    }

    public void setRotationY(float rotationY) {
        mRotationY = rotationY;
        invalidate();
    }

    public float getCameraDistance() {
        return mCameraDistance;
    }

    public void setCameraDistance(float distance) {
        mCameraDistance = distance;
    }

    public boolean isPivotSet() {
        return mPivotSet;
    }

    public void resetPivot() {
        mPivotSet = false;
    }

    public android.graphics.Matrix getAnimationMatrix() {
        return mAnimationMatrix;
    }

    public void setAnimationMatrix(android.graphics.Matrix matrix) {
        mAnimationMatrix = matrix;
        invalidate();
    }

    /** The view's own transform: scale and rotate about the pivot, then translate. */
    public android.graphics.Matrix getMatrix() {
        final android.graphics.Matrix m = new android.graphics.Matrix();
        final float px = getPivotX();
        final float py = getPivotY();
        m.postTranslate(-px, -py);
        m.postScale(mScaleX, mScaleY);
        m.postRotate(mRotation);
        m.postTranslate(px + mTranslationX, py + mTranslationY);
        return m;
    }

    public boolean hasIdentityMatrix() {
        return mScaleX == 1f && mScaleY == 1f && mRotation == 0f
                && mTranslationX == 0f && mTranslationY == 0f;
    }

    public void transformMatrixToGlobal(android.graphics.Matrix matrix) {
        final ViewParent parent = mParent;
        if (parent instanceof View) {
            final View vp = (View) parent;
            vp.transformMatrixToGlobal(matrix);
            matrix.preTranslate(-vp.mScrollX, -vp.mScrollY);
        }
        matrix.preTranslate(mLeft, mTop);
        if (!hasIdentityMatrix()) {
            matrix.preConcat(getMatrix());
        }
    }

    public void transformMatrixToLocal(android.graphics.Matrix matrix) {
        final android.graphics.Matrix inverse = new android.graphics.Matrix();
        final android.graphics.Matrix toGlobal = new android.graphics.Matrix();
        transformMatrixToGlobal(toGlobal);
        if (toGlobal.invert(inverse)) {
            matrix.preConcat(inverse);
        }
    }

    public void getHitRect(android.graphics.Rect outRect) {
        outRect.set(mLeft, mTop, mRight, mBottom);
    }

    public void getDrawingRect(android.graphics.Rect outRect) {
        outRect.left = mScrollX;
        outRect.top = mScrollY;
        outRect.right = mScrollX + (mRight - mLeft);
        outRect.bottom = mScrollY + (mBottom - mTop);
    }

    public void getFocusedRect(android.graphics.Rect r) {
        getDrawingRect(r);
    }

    public boolean getLocalVisibleRect(android.graphics.Rect r) {
        final android.graphics.Point offset = new android.graphics.Point();
        if (getGlobalVisibleRect(r, offset)) {
            r.offset(-offset.x, -offset.y);
            return true;
        }
        return false;
    }

    public boolean getGlobalVisibleRect(android.graphics.Rect r) {
        return getGlobalVisibleRect(r, null);
    }

    public boolean getGlobalVisibleRect(android.graphics.Rect r, android.graphics.Point globalOffset) {
        final int width = mRight - mLeft;
        final int height = mBottom - mTop;
        if (width > 0 && height > 0) {
            r.set(0, 0, width, height);
            if (globalOffset != null) {
                globalOffset.set(-mScrollX, -mScrollY);
            }
            return mParent == null || getParentGroup().getChildVisibleRect(this, r, globalOffset);
        }
        return false;
    }

    public void getWindowVisibleDisplayFrame(android.graphics.Rect outRect) {
        final android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        outRect.set(0, 0, dm.widthPixels, dm.heightPixels);
    }

    public void getLocationInSurface(int[] location) {
        getLocationInWindow(location);
    }

    public int getMeasuredWidthAndState() {
        return mMeasuredWidth;
    }

    public int getMeasuredHeightAndState() {
        return mMeasuredHeight;
    }

    public int getBaseline() {
        return -1;
    }

    public boolean isPaddingRelative() {
        return false;
    }

    protected boolean isPaddingOffsetRequired() {
        return false;
    }

    protected int getLeftPaddingOffset() {
        return 0;
    }

    protected int getRightPaddingOffset() {
        return 0;
    }

    protected int getTopPaddingOffset() {
        return 0;
    }

    protected int getBottomPaddingOffset() {
        return 0;
    }

    public void bringToFront() {
        if (mParent != null) {
            mParent.bringChildToFront(this);
        }
    }

    public View inflate(Context context, int resource, ViewGroup root) {
        return LayoutInflater.from(context).inflate(resource, root);
    }

    public final <T extends View> T requireViewById(int id) {
        final T view = (T) findViewById(id);
        if (view == null) {
            throw new IllegalArgumentException("ID does not reference a View inside this View");
        }
        return view;
    }

    @Override
    public String toString() {
        final StringBuilder out = new StringBuilder(128);
        out.append(getClass().getName());
        out.append('{');
        out.append(Integer.toHexString(System.identityHashCode(this)));
        out.append(' ');
        out.append(mVisibility == VISIBLE ? 'V' : mVisibility == INVISIBLE ? 'I' : 'G');
        out.append(mFocusable ? 'F' : '.');
        out.append(mEnabled ? 'E' : '.');
        out.append(mWillNotDraw ? '.' : 'D');
        out.append(mClickable ? 'C' : '.');
        out.append(mLongClickable ? 'L' : '.');
        out.append(' ');
        out.append(mLeft).append(',').append(mTop).append('-').append(mRight).append(',')
                .append(mBottom);
        if (mId != NO_ID) {
            out.append(" #").append(Integer.toHexString(mId));
            final android.content.res.Resources r = mContext != null ? mContext.getResources() : null;
            if (r != null && (mId >>> 24) != 0) {
                try {
                    final String name = r.getResourceName(mId);
                    final int slash = name.lastIndexOf('/');
                    out.append(" ").append(name.substring(slash + 1));
                } catch (RuntimeException e) {
                    // Unknown id: the hex form above is all there is.
                }
            }
        }
        out.append("}");
        return out.toString();
    }

    // ------------------------------------------------------------------------------------
    // Scrolling.

    private int mOverScrollMode = OVER_SCROLL_ALWAYS;
    private int mScrollBarStyle = SCROLLBARS_INSIDE_OVERLAY;
    private int mScrollBarSize;
    private int mScrollBarDefaultDelayBeforeFade = 300;
    private int mScrollBarFadeDuration = 250;
    private int mVerticalScrollbarPosition = SCROLLBAR_POSITION_DEFAULT;
    private int mScrollIndicators;
    private int mFadingEdgeLength;
    private boolean mHorizontalScrollBarEnabled;
    private boolean mVerticalScrollBarEnabled;
    private boolean mHorizontalFadingEdgeEnabled;
    private boolean mVerticalFadingEdgeEnabled;
    private boolean mScrollbarFadingEnabled = true;
    private OnScrollChangeListener mOnScrollChangeListener;

    public interface OnScrollChangeListener {
        void onScrollChange(View v, int scrollX, int scrollY, int oldScrollX, int oldScrollY);
    }

    public void setOnScrollChangeListener(OnScrollChangeListener l) {
        mOnScrollChangeListener = l;
    }

    public final int getScrollX() {
        return mScrollX;
    }

    public final int getScrollY() {
        return mScrollY;
    }

    public void setScrollX(int value) {
        scrollTo(value, mScrollY);
    }

    public void setScrollY(int value) {
        scrollTo(mScrollX, value);
    }

    protected void onScrollChanged(int l, int t, int oldl, int oldt) {
        if (mOnScrollChangeListener != null) {
            mOnScrollChangeListener.onScrollChange(this, l, t, oldl, oldt);
        }
    }

    public void computeScroll() {
    }

    protected int computeHorizontalScrollRange() {
        return getWidth();
    }

    protected int computeHorizontalScrollOffset() {
        return mScrollX;
    }

    protected int computeHorizontalScrollExtent() {
        return getWidth();
    }

    protected int computeVerticalScrollRange() {
        return getHeight();
    }

    protected int computeVerticalScrollOffset() {
        return mScrollY;
    }

    protected int computeVerticalScrollExtent() {
        return getHeight();
    }

    public boolean canScrollHorizontally(int direction) {
        final int offset = computeHorizontalScrollOffset();
        final int range = computeHorizontalScrollRange() - computeHorizontalScrollExtent();
        if (range == 0) return false;
        if (direction < 0) {
            return offset > 0;
        }
        return offset < range - 1;
    }

    public boolean canScrollVertically(int direction) {
        final int offset = computeVerticalScrollOffset();
        final int range = computeVerticalScrollRange() - computeVerticalScrollExtent();
        if (range == 0) return false;
        if (direction < 0) {
            return offset > 0;
        }
        return offset < range - 1;
    }

    protected boolean awakenScrollBars() {
        return false;
    }

    protected boolean awakenScrollBars(int startDelay) {
        return false;
    }

    protected boolean awakenScrollBars(int startDelay, boolean invalidate) {
        return false;
    }

    protected void onOverScrolled(int scrollX, int scrollY, boolean clampedX, boolean clampedY) {
    }

    protected boolean overScrollBy(int deltaX, int deltaY, int scrollX, int scrollY,
            int scrollRangeX, int scrollRangeY, int maxOverScrollX, int maxOverScrollY,
            boolean isTouchEvent) {
        final int overScrollMode = mOverScrollMode;
        final boolean canScrollHorizontal = computeHorizontalScrollRange() > computeHorizontalScrollExtent();
        final boolean canScrollVertical = computeVerticalScrollRange() > computeVerticalScrollExtent();
        final boolean overScrollHorizontal = overScrollMode == OVER_SCROLL_ALWAYS
                || (overScrollMode == OVER_SCROLL_IF_CONTENT_SCROLLS && canScrollHorizontal);
        final boolean overScrollVertical = overScrollMode == OVER_SCROLL_ALWAYS
                || (overScrollMode == OVER_SCROLL_IF_CONTENT_SCROLLS && canScrollVertical);

        int newScrollX = scrollX + deltaX;
        if (!overScrollHorizontal) {
            maxOverScrollX = 0;
        }
        int newScrollY = scrollY + deltaY;
        if (!overScrollVertical) {
            maxOverScrollY = 0;
        }
        final int left = -maxOverScrollX;
        final int right = maxOverScrollX + scrollRangeX;
        final int top = -maxOverScrollY;
        final int bottom = maxOverScrollY + scrollRangeY;

        boolean clampedX = false;
        if (newScrollX > right) {
            newScrollX = right;
            clampedX = true;
        } else if (newScrollX < left) {
            newScrollX = left;
            clampedX = true;
        }
        boolean clampedY = false;
        if (newScrollY > bottom) {
            newScrollY = bottom;
            clampedY = true;
        } else if (newScrollY < top) {
            newScrollY = top;
            clampedY = true;
        }
        onOverScrolled(newScrollX, newScrollY, clampedX, clampedY);
        return clampedX || clampedY;
    }

    public int getOverScrollMode() {
        return mOverScrollMode;
    }

    public void setOverScrollMode(int overScrollMode) {
        if (overScrollMode != OVER_SCROLL_ALWAYS && overScrollMode != OVER_SCROLL_IF_CONTENT_SCROLLS
                && overScrollMode != OVER_SCROLL_NEVER) {
            throw new IllegalArgumentException("Invalid overscroll mode " + overScrollMode);
        }
        mOverScrollMode = overScrollMode;
    }

    public boolean isHorizontalScrollBarEnabled() {
        return mHorizontalScrollBarEnabled;
    }

    public void setHorizontalScrollBarEnabled(boolean enabled) {
        mHorizontalScrollBarEnabled = enabled;
    }

    public boolean isVerticalScrollBarEnabled() {
        return mVerticalScrollBarEnabled;
    }

    public void setVerticalScrollBarEnabled(boolean enabled) {
        mVerticalScrollBarEnabled = enabled;
    }

    public boolean isHorizontalFadingEdgeEnabled() {
        return mHorizontalFadingEdgeEnabled;
    }

    public void setHorizontalFadingEdgeEnabled(boolean enabled) {
        mHorizontalFadingEdgeEnabled = enabled;
    }

    public boolean isVerticalFadingEdgeEnabled() {
        return mVerticalFadingEdgeEnabled;
    }

    public void setVerticalFadingEdgeEnabled(boolean enabled) {
        mVerticalFadingEdgeEnabled = enabled;
    }

    public int getVerticalFadingEdgeLength() {
        return mFadingEdgeLength;
    }

    public int getHorizontalFadingEdgeLength() {
        return mFadingEdgeLength;
    }

    public void setFadingEdgeLength(int length) {
        mFadingEdgeLength = length;
    }

    protected float getTopFadingEdgeStrength() {
        return computeVerticalScrollOffset() > 0 ? 1.0f : 0.0f;
    }

    protected float getBottomFadingEdgeStrength() {
        return computeVerticalScrollOffset() + computeVerticalScrollExtent()
                < computeVerticalScrollRange() ? 1.0f : 0.0f;
    }

    protected float getLeftFadingEdgeStrength() {
        return computeHorizontalScrollOffset() > 0 ? 1.0f : 0.0f;
    }

    protected float getRightFadingEdgeStrength() {
        return computeHorizontalScrollOffset() + computeHorizontalScrollExtent()
                < computeHorizontalScrollRange() ? 1.0f : 0.0f;
    }

    public int getScrollBarStyle() {
        return mScrollBarStyle;
    }

    public void setScrollBarStyle(int style) {
        mScrollBarStyle = style;
    }

    public int getScrollBarSize() {
        return mScrollBarSize;
    }

    public void setScrollBarSize(int scrollBarSize) {
        mScrollBarSize = scrollBarSize;
    }

    public int getScrollBarDefaultDelayBeforeFade() {
        return mScrollBarDefaultDelayBeforeFade;
    }

    public void setScrollBarDefaultDelayBeforeFade(int delay) {
        mScrollBarDefaultDelayBeforeFade = delay;
    }

    public int getScrollBarFadeDuration() {
        return mScrollBarFadeDuration;
    }

    public void setScrollBarFadeDuration(int duration) {
        mScrollBarFadeDuration = duration;
    }

    public boolean isScrollbarFadingEnabled() {
        return mScrollbarFadingEnabled;
    }

    public void setScrollbarFadingEnabled(boolean fadeScrollbars) {
        mScrollbarFadingEnabled = fadeScrollbars;
    }

    public int getVerticalScrollbarPosition() {
        return mVerticalScrollbarPosition;
    }

    public void setVerticalScrollbarPosition(int position) {
        mVerticalScrollbarPosition = position;
    }

    protected int getVerticalScrollbarWidth() {
        return mScrollBarSize;
    }

    protected int getHorizontalScrollbarHeight() {
        return mScrollBarSize;
    }

    public void setVerticalScrollbarThumbDrawable(android.graphics.drawable.Drawable drawable) {
    }

    public void setVerticalScrollbarTrackDrawable(android.graphics.drawable.Drawable drawable) {
    }

    public void setHorizontalScrollbarThumbDrawable(android.graphics.drawable.Drawable drawable) {
    }

    public void setHorizontalScrollbarTrackDrawable(android.graphics.drawable.Drawable drawable) {
    }

    protected void initializeScrollbars(android.content.res.TypedArray a) {
    }

    protected void initializeFadingEdge(android.content.res.TypedArray a) {
    }

    public int getScrollIndicators() {
        return mScrollIndicators;
    }

    public void setScrollIndicators(int indicators) {
        mScrollIndicators = indicators;
    }

    public void setScrollIndicators(int indicators, int mask) {
        mScrollIndicators = (mScrollIndicators & ~mask) | (indicators & mask);
    }

    protected void onDrawScrollBars(Canvas canvas) {
    }

    public void onDrawForeground(Canvas canvas) {
        if (mForeground != null) {
            mForeground.setBounds(0, 0, getWidth(), getHeight());
            mForeground.draw(canvas);
        }
    }

    protected void dispatchDraw(Canvas canvas) {
    }

    // ------------------------------------------------------------------------------------
    // Invalidation.

    public void invalidate(android.graphics.Rect dirty) {
        invalidate();
    }

    public void invalidate(int l, int t, int r, int b) {
        invalidate();
    }

    public void postInvalidate(int left, int top, int right, int bottom) {
        postInvalidate();
    }

    public void postInvalidateDelayed(long delayMilliseconds) {
        postDelayed(new Runnable() { public void run() { invalidate(); } }, delayMilliseconds);
    }

    public void postInvalidateDelayed(long delayMilliseconds, int left, int top, int right,
            int bottom) {
        postInvalidateDelayed(delayMilliseconds);
    }

    public void postInvalidateOnAnimation() {
        postInvalidate();
    }

    public void postInvalidateOnAnimation(int left, int top, int right, int bottom) {
        postInvalidate();
    }

    // ------------------------------------------------------------------------------------
    // Drawable state and Drawable.Callback.

    protected static final int[] EMPTY_STATE_SET = {};
    protected static final int[] WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused};
    protected static final int[] SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected};
    protected static final int[] SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected};
    protected static final int[] FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_focused};
    protected static final int[] FOCUSED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_focused};
    protected static final int[] FOCUSED_SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused};
    protected static final int[] FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused};
    protected static final int[] ENABLED_STATE_SET = {
            com.android.internal.R.attr.state_enabled};
    protected static final int[] ENABLED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] ENABLED_SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] ENABLED_SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] ENABLED_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] ENABLED_FOCUSED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] ENABLED_FOCUSED_SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] ENABLED_FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_STATE_SET = {
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_FOCUSED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_FOCUSED_SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed};
    protected static final int[] PRESSED_ENABLED_STATE_SET = {
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_ENABLED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_ENABLED_SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_ENABLED_SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_ENABLED_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_ENABLED_FOCUSED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_ENABLED_FOCUSED_SELECTED_STATE_SET = {
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};
    protected static final int[] PRESSED_ENABLED_FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET = {
            com.android.internal.R.attr.state_window_focused,
            com.android.internal.R.attr.state_selected,
            com.android.internal.R.attr.state_focused,
            com.android.internal.R.attr.state_pressed,
            com.android.internal.R.attr.state_enabled};

    private int[] mDrawableState;

    /** The current state as drawable state attribute ids, rebuilt after refreshDrawableState. */
    public final int[] getDrawableState() {
        if (mDrawableState != null) {
            return mDrawableState;
        }
        mDrawableState = onCreateDrawableState(0);
        return mDrawableState;
    }

    /**
     * Builds the state array. extraSpace trailing zeros are left for subclasses, which fill them
     * through mergeDrawableStates.
     */
    protected int[] onCreateDrawableState(int extraSpace) {
        if (mDuplicateParentState && mParent instanceof View) {
            return ((View) mParent).onCreateDrawableState(extraSpace);
        }
        final int[] states = new int[8 + extraSpace];
        int n = 0;
        if (hasWindowFocus()) states[n++] = com.android.internal.R.attr.state_window_focused;
        if (mSelected) states[n++] = com.android.internal.R.attr.state_selected;
        if (mHasFocus) states[n++] = com.android.internal.R.attr.state_focused;
        if (mEnabled) states[n++] = com.android.internal.R.attr.state_enabled;
        if (mPressed) states[n++] = com.android.internal.R.attr.state_pressed;
        if (mActivated) states[n++] = com.android.internal.R.attr.state_activated;
        if (mHovered) states[n++] = com.android.internal.R.attr.state_hovered;
        final int[] result = new int[n + extraSpace];
        System.arraycopy(states, 0, result, 0, n);
        return result;
    }

    protected static int[] mergeDrawableStates(int[] baseState, int[] additionalState) {
        int i = baseState.length - 1;
        while (i >= 0 && baseState[i] == 0) {
            i--;
        }
        System.arraycopy(additionalState, 0, baseState, i + 1, additionalState.length);
        return baseState;
    }

    /** Recomputes the drawable state and pushes it to the drawables that depend on it. */
    public void refreshDrawableState() {
        mDrawableState = null;
        drawableStateChanged();
        if (mParent != null) {
            mParent.childDrawableStateChanged(this);
        }
    }

    protected void drawableStateChanged() {
        final int[] state = getDrawableState();
        boolean changed = false;
        if (mBackground != null && mBackground.isStateful()) {
            changed |= mBackground.setState(state);
        }
        if (mForeground != null && mForeground.isStateful()) {
            changed |= mForeground.setState(state);
        }
        if (mStateListAnimator != null) {
            mStateListAnimator.setState(state);
        }
        if (changed) {
            invalidate();
        }
    }

    public void jumpDrawablesToCurrentState() {
        if (mBackground != null) mBackground.jumpToCurrentState();
        if (mStateListAnimator != null) mStateListAnimator.jumpToCurrentState();
        if (mForeground != null) mForeground.jumpToCurrentState();
    }

    public void drawableHotspotChanged(float x, float y) {
        if (mBackground != null) mBackground.setHotspot(x, y);
        if (mForeground != null) mForeground.setHotspot(x, y);
        dispatchDrawableHotspotChanged(x, y);
    }

    public void dispatchDrawableHotspotChanged(float x, float y) {
    }

    protected boolean verifyDrawable(android.graphics.drawable.Drawable who) {
        return who == mBackground || who == mForeground || who == mScrollIndicatorDrawable;
    }

    @Override
    public void invalidateDrawable(android.graphics.drawable.Drawable drawable) {
        if (verifyDrawable(drawable)) {
            invalidate();
        }
    }

    @Override
    public void scheduleDrawable(android.graphics.drawable.Drawable who, Runnable what, long when) {
        if (verifyDrawable(who) && what != null) {
            final long delay = when - android.os.SystemClock.uptimeMillis();
            postDelayed(what, Math.max(0L, delay));
        }
    }

    @Override
    public void unscheduleDrawable(android.graphics.drawable.Drawable who, Runnable what) {
        if (verifyDrawable(who) && what != null) {
            removeCallbacks(what);
        }
    }

    public void unscheduleDrawable(android.graphics.drawable.Drawable who) {
    }

    // Tints.

    private android.content.res.ColorStateList mBackgroundTintList;
    private android.graphics.PorterDuff.Mode mBackgroundTintMode;
    private android.content.res.ColorStateList mForegroundTintList;
    private android.graphics.PorterDuff.Mode mForegroundTintMode;
    private int mForegroundGravity = Gravity.FILL;

    public android.content.res.ColorStateList getBackgroundTintList() {
        return mBackgroundTintList;
    }

    public void setBackgroundTintList(android.content.res.ColorStateList tint) {
        mBackgroundTintList = tint;
        applyBackgroundTint();
    }

    public android.graphics.PorterDuff.Mode getBackgroundTintMode() {
        return mBackgroundTintMode;
    }

    public void setBackgroundTintMode(android.graphics.PorterDuff.Mode tintMode) {
        mBackgroundTintMode = tintMode;
        applyBackgroundTint();
    }

    public void setBackgroundTintBlendMode(android.graphics.BlendMode blendMode) {
        setBackgroundTintMode(android.graphics.BlendMode.blendModeToPorterDuffMode(blendMode));
    }

    private void applyBackgroundTint() {
        if (mBackground != null && (mBackgroundTintList != null || mBackgroundTintMode != null)) {
            mBackground = mBackground.mutate();
            mBackground.setTintList(mBackgroundTintList);
            mBackground.setTintMode(mBackgroundTintMode);
            if (mBackground.isStateful()) {
                mBackground.setState(getDrawableState());
            }
            invalidate();
        }
    }

    public android.content.res.ColorStateList getForegroundTintList() {
        return mForegroundTintList;
    }

    public void setForegroundTintList(android.content.res.ColorStateList tint) {
        mForegroundTintList = tint;
        applyForegroundTint();
    }

    public android.graphics.PorterDuff.Mode getForegroundTintMode() {
        return mForegroundTintMode;
    }

    public void setForegroundTintMode(android.graphics.PorterDuff.Mode tintMode) {
        mForegroundTintMode = tintMode;
        applyForegroundTint();
    }

    public void setForegroundTintBlendMode(android.graphics.BlendMode blendMode) {
        setForegroundTintMode(android.graphics.BlendMode.blendModeToPorterDuffMode(blendMode));
    }

    private void applyForegroundTint() {
        if (mForeground != null && (mForegroundTintList != null || mForegroundTintMode != null)) {
            mForeground = mForeground.mutate();
            mForeground.setTintList(mForegroundTintList);
            mForeground.setTintMode(mForegroundTintMode);
            if (mForeground.isStateful()) {
                mForeground.setState(getDrawableState());
            }
            invalidate();
        }
    }

    public int getForegroundGravity() {
        return mForegroundGravity;
    }

    public void setForegroundGravity(int gravity) {
        mForegroundGravity = gravity;
    }

    // ------------------------------------------------------------------------------------
    // Focus.

    private int mNextFocusLeftId = NO_ID;
    private int mNextFocusRightId = NO_ID;
    private int mNextFocusUpId = NO_ID;
    private int mNextFocusDownId = NO_ID;
    private int mNextFocusForwardId = NO_ID;
    private int mNextClusterForwardId = NO_ID;

    public int getNextFocusLeftId() { return mNextFocusLeftId; }
    public void setNextFocusLeftId(int id) { mNextFocusLeftId = id; }
    public int getNextFocusRightId() { return mNextFocusRightId; }
    public void setNextFocusRightId(int id) { mNextFocusRightId = id; }
    public int getNextFocusUpId() { return mNextFocusUpId; }
    public void setNextFocusUpId(int id) { mNextFocusUpId = id; }
    public int getNextFocusDownId() { return mNextFocusDownId; }
    public void setNextFocusDownId(int id) { mNextFocusDownId = id; }
    public int getNextFocusForwardId() { return mNextFocusForwardId; }
    public void setNextFocusForwardId(int id) { mNextFocusForwardId = id; }
    public int getNextClusterForwardId() { return mNextClusterForwardId; }
    public void setNextClusterForwardId(int id) { mNextClusterForwardId = id; }

    public OnFocusChangeListener getOnFocusChangeListener() {
        return mOnFocusChangeListener;
    }

    public int getFocusable() {
        return mFocusable ? FOCUSABLE : NOT_FOCUSABLE;
    }

    public boolean requestFocus(int direction) {
        return requestFocus(direction, null);
    }

    public boolean requestFocus(int direction, android.graphics.Rect previouslyFocusedRect) {
        if (!mFocusable || mVisibility != VISIBLE) {
            return false;
        }
        return requestFocus();
    }

    public final boolean requestFocusFromTouch() {
        return requestFocus(FOCUS_DOWN);
    }

    public boolean restoreDefaultFocus() {
        return requestFocus(FOCUS_FORWARD);
    }

    public View findFocus() {
        return mHasFocus ? this : null;
    }

    public View focusSearch(int direction) {
        return mParent != null ? mParent.focusSearch(this, direction) : null;
    }

    public boolean hasFocusable() {
        return mFocusable && mVisibility == VISIBLE;
    }

    public boolean hasExplicitFocusable() {
        return hasFocusable();
    }

    public java.util.ArrayList<View> getFocusables(int direction) {
        final java.util.ArrayList<View> result = new java.util.ArrayList<View>(24);
        addFocusables(result, direction);
        return result;
    }

    public void addFocusables(java.util.ArrayList<View> views, int direction) {
        addFocusables(views, direction, FOCUSABLES_TOUCH_MODE);
    }

    public void addFocusables(java.util.ArrayList<View> views, int direction, int focusableMode) {
        if (views == null || !mFocusable || mVisibility != VISIBLE || !mEnabled) {
            return;
        }
        if ((focusableMode & FOCUSABLES_TOUCH_MODE) == FOCUSABLES_TOUCH_MODE && isInTouchMode()
                && !mFocusableInTouchMode) {
            return;
        }
        views.add(this);
    }

    public java.util.ArrayList<View> getTouchables() {
        final java.util.ArrayList<View> result = new java.util.ArrayList<View>();
        addTouchables(result);
        return result;
    }

    public void addTouchables(java.util.ArrayList<View> views) {
        if (mVisibility == VISIBLE && mEnabled && (mClickable || mLongClickable)) {
            views.add(this);
        }
    }

    public void findViewsWithText(java.util.ArrayList<View> outViews, CharSequence searched,
            int flags) {
        if ((flags & FIND_VIEWS_WITH_CONTENT_DESCRIPTION) != 0 && searched != null
                && searched.length() > 0 && mContentDescription != null
                && mContentDescription.length() > 0) {
            final String searchedLowerCase = searched.toString().toLowerCase();
            final String contentDescriptionLowerCase = mContentDescription.toString().toLowerCase();
            if (contentDescriptionLowerCase.contains(searchedLowerCase)) {
                outViews.add(this);
            }
        }
    }

    public static final int FIND_VIEWS_WITH_TEXT = 0x00000001;
    public static final int FIND_VIEWS_WITH_CONTENT_DESCRIPTION = 0x00000002;

    public View keyboardNavigationClusterSearch(View currentCluster, int direction) {
        return null;
    }

    public void addKeyboardNavigationClusters(java.util.Collection<View> views, int direction) {
    }

    public boolean dispatchUnhandledMove(View focused, int direction) {
        return false;
    }

    public boolean dispatchKeyEventPreIme(KeyEvent event) {
        return onKeyPreIme(event.getKeyCode(), event);
    }

    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean dispatchKeyShortcutEvent(KeyEvent event) {
        return onKeyShortcut(event.getKeyCode(), event);
    }

    public boolean onKeyShortcut(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean dispatchTrackballEvent(MotionEvent event) {
        return onTrackballEvent(event);
    }

    public boolean onTrackballEvent(MotionEvent event) {
        return false;
    }

    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (mOnGenericMotionListener != null && mOnGenericMotionListener.onGenericMotion(this, event)) {
            return true;
        }
        return onGenericMotionEvent(event);
    }

    public boolean onGenericMotionEvent(MotionEvent event) {
        return false;
    }

    protected boolean dispatchHoverEvent(MotionEvent event) {
        if (mOnHoverListener != null && mOnHoverListener.onHover(this, event)) {
            return true;
        }
        return onHoverEvent(event);
    }

    public boolean onHoverEvent(MotionEvent event) {
        return false;
    }

    protected boolean dispatchGenericPointerEvent(MotionEvent event) {
        return false;
    }

    protected boolean dispatchGenericFocusedEvent(MotionEvent event) {
        return false;
    }

    public boolean dispatchCapturedPointerEvent(MotionEvent event) {
        return onCapturedPointerEvent(event);
    }

    public boolean onCapturedPointerEvent(MotionEvent event) {
        return false;
    }

    public void dispatchPointerCaptureChanged(boolean hasCapture) {
        onPointerCaptureChange(hasCapture);
    }

    public void onPointerCaptureChange(boolean hasCapture) {
    }

    public void requestPointerCapture() {
    }

    public void releasePointerCapture() {
    }

    public boolean hasPointerCapture() {
        return false;
    }

    public void requestUnbufferedDispatch(MotionEvent event) {
    }

    public boolean dispatchDragEvent(DragEvent event) {
        if (mOnDragListener != null && mOnDragListener.onDrag(this, event)) {
            return true;
        }
        return onDragEvent(event);
    }

    public boolean onDragEvent(DragEvent event) {
        return false;
    }

    public void cancelDragAndDrop() {
    }

    public void updateDragShadow(DragShadowBuilder shadowBuilder) {
    }

    public final boolean startDragAndDrop(android.content.ClipData data,
            DragShadowBuilder shadowBuilder, Object myLocalState, int flags) {
        return false;
    }

    public final boolean startDrag(android.content.ClipData data, DragShadowBuilder shadowBuilder,
            Object myLocalState, int flags) {
        return startDragAndDrop(data, shadowBuilder, myLocalState, flags);
    }

    public PointerIcon getPointerIcon() {
        return mPointerIcon;
    }

    public void setPointerIcon(PointerIcon pointerIcon) {
        mPointerIcon = pointerIcon;
    }

    public PointerIcon onResolvePointerIcon(MotionEvent event, int pointerIndex) {
        return null;
    }

    private PointerIcon mPointerIcon;

    public interface OnHoverListener {
        boolean onHover(View v, MotionEvent event);
    }

    public interface OnGenericMotionListener {
        boolean onGenericMotion(View v, MotionEvent event);
    }

    public interface OnContextClickListener {
        boolean onContextClick(View v);
    }

    public interface OnCapturedPointerListener {
        boolean onCapturedPointer(View view, MotionEvent event);
    }

    public interface OnCreateContextMenuListener {
        void onCreateContextMenu(ContextMenu menu, View v, ContextMenu.ContextMenuInfo menuInfo);
    }

    public interface OnUnhandledKeyEventListener {
        boolean onUnhandledKeyEvent(View v, KeyEvent event);
    }

    private OnHoverListener mOnHoverListener;
    private OnGenericMotionListener mOnGenericMotionListener;
    private OnContextClickListener mOnContextClickListener;
    private OnCapturedPointerListener mOnCapturedPointerListener;
    private OnCreateContextMenuListener mOnCreateContextMenuListener;
    private java.util.ArrayList<OnUnhandledKeyEventListener> mUnhandledKeyListeners;
    private OnDragListener mOnDragListener;

    public void setOnHoverListener(OnHoverListener l) {
        mOnHoverListener = l;
    }

    public void setOnGenericMotionListener(OnGenericMotionListener l) {
        mOnGenericMotionListener = l;
    }

    public void setOnContextClickListener(OnContextClickListener l) {
        if (!mContextClickable) mContextClickable = true;
        mOnContextClickListener = l;
    }

    public void setOnCapturedPointerListener(OnCapturedPointerListener l) {
        mOnCapturedPointerListener = l;
    }

    public void setOnCreateContextMenuListener(OnCreateContextMenuListener l) {
        mOnCreateContextMenuListener = l;
    }

    public void setOnDragListener(OnDragListener l) {
        mOnDragListener = l;
    }

    public void addOnUnhandledKeyEventListener(OnUnhandledKeyEventListener listener) {
        if (mUnhandledKeyListeners == null) {
            mUnhandledKeyListeners = new java.util.ArrayList<OnUnhandledKeyEventListener>();
        }
        mUnhandledKeyListeners.add(listener);
    }

    public void removeOnUnhandledKeyEventListener(OnUnhandledKeyEventListener listener) {
        if (mUnhandledKeyListeners != null) mUnhandledKeyListeners.remove(listener);
    }

    public boolean performContextClick(float x, float y) {
        return performContextClick();
    }

    public boolean performContextClick() {
        if (mOnContextClickListener != null) {
            return mOnContextClickListener.onContextClick(this);
        }
        return false;
    }

    public boolean performLongClick(float x, float y) {
        return performLongClick();
    }

    public boolean showContextMenu() {
        return mParent != null && mParent.showContextMenuForChild(this);
    }

    public boolean showContextMenu(float x, float y) {
        return showContextMenu();
    }

    protected ContextMenu.ContextMenuInfo getContextMenuInfo() {
        return null;
    }

    public void createContextMenu(ContextMenu menu) {
        final ContextMenu.ContextMenuInfo menuInfo = getContextMenuInfo();
        onCreateContextMenu(menu);
        if (mOnCreateContextMenuListener != null) {
            mOnCreateContextMenuListener.onCreateContextMenu(menu, this, menuInfo);
        }
    }

    protected void onCreateContextMenu(ContextMenu menu) {
    }

    public ActionMode startActionMode(ActionMode.Callback callback) {
        return startActionMode(callback, ActionMode.TYPE_PRIMARY);
    }

    public ActionMode startActionMode(ActionMode.Callback callback, int type) {
        return mParent != null ? mParent.startActionModeForChild(this, callback, type) : null;
    }

    public boolean requestRectangleOnScreen(android.graphics.Rect rectangle) {
        return requestRectangleOnScreen(rectangle, false);
    }

    public boolean requestRectangleOnScreen(android.graphics.Rect rectangle, boolean immediate) {
        if (mParent == null) {
            return false;
        }
        View child = this;
        final android.graphics.RectF position = new android.graphics.RectF();
        position.set(rectangle);
        ViewParent parent = mParent;
        boolean scrolled = false;
        while (parent != null) {
            rectangle.set((int) position.left, (int) position.top,
                    (int) position.right, (int) position.bottom);
            scrolled |= parent.requestChildRectangleOnScreen(child, rectangle, immediate);
            if (!(parent instanceof View)) {
                break;
            }
            position.offset(child.getLeft() - child.getScrollX(), child.getTop() - child.getScrollY());
            child = (View) parent;
            parent = child.getParent();
        }
        return scrolled;
    }

    // ------------------------------------------------------------------------------------
    // Nested scrolling (the child side of the protocol).

    private boolean mNestedScrollingEnabled;
    private ViewParent mNestedScrollingParent;

    public void setNestedScrollingEnabled(boolean enabled) {
        if (!enabled) {
            stopNestedScroll();
        }
        mNestedScrollingEnabled = enabled;
    }

    public boolean isNestedScrollingEnabled() {
        return mNestedScrollingEnabled;
    }

    public boolean startNestedScroll(int axes) {
        if (hasNestedScrollingParent()) {
            return true;
        }
        if (isNestedScrollingEnabled()) {
            ViewParent p = getParent();
            View child = this;
            while (p != null) {
                if (p.onStartNestedScroll(child, this, axes)) {
                    mNestedScrollingParent = p;
                    p.onNestedScrollAccepted(child, this, axes);
                    return true;
                }
                if (p instanceof View) {
                    child = (View) p;
                }
                p = p.getParent();
            }
        }
        return false;
    }

    public void stopNestedScroll() {
        if (mNestedScrollingParent != null) {
            mNestedScrollingParent.onStopNestedScroll(this);
            mNestedScrollingParent = null;
        }
    }

    public boolean hasNestedScrollingParent() {
        return mNestedScrollingParent != null;
    }

    public boolean dispatchNestedScroll(int dxConsumed, int dyConsumed, int dxUnconsumed,
            int dyUnconsumed, int[] offsetInWindow) {
        if (isNestedScrollingEnabled() && mNestedScrollingParent != null) {
            if (dxConsumed != 0 || dyConsumed != 0 || dxUnconsumed != 0 || dyUnconsumed != 0) {
                int startX = 0;
                int startY = 0;
                if (offsetInWindow != null) {
                    getLocationInWindow(offsetInWindow);
                    startX = offsetInWindow[0];
                    startY = offsetInWindow[1];
                }
                mNestedScrollingParent.onNestedScroll(this, dxConsumed, dyConsumed,
                        dxUnconsumed, dyUnconsumed);
                if (offsetInWindow != null) {
                    getLocationInWindow(offsetInWindow);
                    offsetInWindow[0] -= startX;
                    offsetInWindow[1] -= startY;
                }
                return true;
            } else if (offsetInWindow != null) {
                offsetInWindow[0] = 0;
                offsetInWindow[1] = 0;
            }
        }
        return false;
    }

    public boolean dispatchNestedPreScroll(int dx, int dy, int[] consumed, int[] offsetInWindow) {
        if (isNestedScrollingEnabled() && mNestedScrollingParent != null) {
            if (dx != 0 || dy != 0) {
                int startX = 0;
                int startY = 0;
                if (offsetInWindow != null) {
                    getLocationInWindow(offsetInWindow);
                    startX = offsetInWindow[0];
                    startY = offsetInWindow[1];
                }
                if (consumed == null) {
                    consumed = new int[2];
                }
                consumed[0] = 0;
                consumed[1] = 0;
                mNestedScrollingParent.onNestedPreScroll(this, dx, dy, consumed);
                if (offsetInWindow != null) {
                    getLocationInWindow(offsetInWindow);
                    offsetInWindow[0] -= startX;
                    offsetInWindow[1] -= startY;
                }
                return consumed[0] != 0 || consumed[1] != 0;
            } else if (offsetInWindow != null) {
                offsetInWindow[0] = 0;
                offsetInWindow[1] = 0;
            }
        }
        return false;
    }

    public boolean dispatchNestedFling(float velocityX, float velocityY, boolean consumed) {
        if (isNestedScrollingEnabled() && mNestedScrollingParent != null) {
            return mNestedScrollingParent.onNestedFling(this, velocityX, velocityY, consumed);
        }
        return false;
    }

    public boolean dispatchNestedPreFling(float velocityX, float velocityY) {
        if (isNestedScrollingEnabled() && mNestedScrollingParent != null) {
            return mNestedScrollingParent.onNestedPreFling(this, velocityX, velocityY);
        }
        return false;
    }

    public boolean dispatchNestedPrePerformAccessibilityAction(int action, Bundle arguments) {
        ViewParent p = getParent();
        while (p != null) {
            if (p.onNestedPrePerformAccessibilityAction(this, action, arguments)) {
                return true;
            }
            p = p.getParent();
        }
        return false;
    }

    // ------------------------------------------------------------------------------------
    // Accessibility, autofill and text/layout direction bookkeeping.

    private int mImportantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_AUTO;
    private int mImportantForAutofill = IMPORTANT_FOR_AUTOFILL_AUTO;
    private int mAccessibilityLiveRegion = ACCESSIBILITY_LIVE_REGION_NONE;
    private int mAccessibilityTraversalBefore = NO_ID;
    private int mAccessibilityTraversalAfter = NO_ID;
    private boolean mAccessibilityHeading;
    private CharSequence mAccessibilityPaneTitle;
    private int mTextDirection = TEXT_DIRECTION_INHERIT;
    private int mTextAlignment = TEXT_ALIGNMENT_GRAVITY;
    private int mLayoutDirection = LAYOUT_DIRECTION_LTR;

    public int getImportantForAccessibility() {
        return mImportantForAccessibility;
    }

    public void setImportantForAccessibility(int mode) {
        mImportantForAccessibility = mode;
    }

    /** True when accessibility services should see this view, resolving the AUTO mode. */
    public boolean isImportantForAccessibility() {
        if (mImportantForAccessibility == IMPORTANT_FOR_ACCESSIBILITY_NO
                || mImportantForAccessibility == IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS) {
            return false;
        }
        ViewParent parent = mParent;
        while (parent instanceof View) {
            if (((View) parent).getImportantForAccessibility()
                    == IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS) {
                return false;
            }
            parent = parent.getParent();
        }
        return mImportantForAccessibility == IMPORTANT_FOR_ACCESSIBILITY_YES
                || mClickable || mLongClickable || mFocusable || mContentDescription != null;
    }

    public int getImportantForAutofill() {
        return mImportantForAutofill;
    }

    public void setImportantForAutofill(int mode) {
        mImportantForAutofill = mode;
    }

    public boolean isImportantForAutofill() {
        return mImportantForAutofill != IMPORTANT_FOR_AUTOFILL_NO
                && mImportantForAutofill != IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS;
    }

    public void setAutofillHints(String... autofillHints) {
    }

    public AccessibilityDelegate getAccessibilityDelegate() {
        return mAccessibilityDelegate;
    }

    public CharSequence getAccessibilityClassName() {
        return View.class.getName();
    }

    public int getAccessibilityLiveRegion() {
        return mAccessibilityLiveRegion;
    }

    public void setAccessibilityLiveRegion(int mode) {
        mAccessibilityLiveRegion = mode;
    }

    public boolean isAccessibilityHeading() {
        return mAccessibilityHeading;
    }

    public void setAccessibilityHeading(boolean isHeading) {
        mAccessibilityHeading = isHeading;
    }

    public CharSequence getAccessibilityPaneTitle() {
        return mAccessibilityPaneTitle;
    }

    public void setAccessibilityPaneTitle(CharSequence accessibilityPaneTitle) {
        mAccessibilityPaneTitle = accessibilityPaneTitle;
    }

    public int getAccessibilityTraversalBefore() {
        return mAccessibilityTraversalBefore;
    }

    public void setAccessibilityTraversalBefore(int beforeId) {
        mAccessibilityTraversalBefore = beforeId;
    }

    public int getAccessibilityTraversalAfter() {
        return mAccessibilityTraversalAfter;
    }

    public void setAccessibilityTraversalAfter(int afterId) {
        mAccessibilityTraversalAfter = afterId;
    }

    public boolean isAccessibilityFocused() {
        return false;
    }

    public ViewParent getParentForAccessibility() {
        ViewParent parent = mParent;
        while (parent instanceof View) {
            if (((View) parent).isImportantForAccessibility()) {
                return parent;
            }
            parent = parent.getParent();
        }
        return null;
    }

    public void sendAccessibilityEvent(int eventType) {
        if (mAccessibilityDelegate != null) {
            mAccessibilityDelegate.sendAccessibilityEvent(this, eventType);
        }
    }

    public void sendAccessibilityEventUnchecked(android.view.accessibility.AccessibilityEvent event) {
    }

    public void announceForAccessibility(CharSequence text) {
    }

    public boolean performAccessibilityAction(int action, Bundle arguments) {
        if (mAccessibilityDelegate != null) {
            return mAccessibilityDelegate.performAccessibilityAction(this, action, arguments);
        }
        return false;
    }

    public boolean dispatchPopulateAccessibilityEvent(
            android.view.accessibility.AccessibilityEvent event) {
        return false;
    }

    public void onPopulateAccessibilityEvent(android.view.accessibility.AccessibilityEvent event) {
    }

    public void onInitializeAccessibilityEvent(android.view.accessibility.AccessibilityEvent event) {
    }

    public void onInitializeAccessibilityNodeInfo(
            android.view.accessibility.AccessibilityNodeInfo info) {
    }

    public android.view.accessibility.AccessibilityNodeInfo createAccessibilityNodeInfo() {
        return null;
    }

    public android.view.accessibility.AccessibilityNodeProvider getAccessibilityNodeProvider() {
        return mAccessibilityDelegate != null
                ? mAccessibilityDelegate.getAccessibilityNodeProvider(this) : null;
    }

    public void addChildrenForAccessibility(java.util.ArrayList<View> outChildren) {
    }

    public void notifySubtreeAccessibilityStateChanged(View child, View source, int changeType) {
    }

    public int getTextDirection() {
        return mTextDirection;
    }

    public void setTextDirection(int textDirection) {
        mTextDirection = textDirection;
    }

    public int getRawTextDirection() {
        return mTextDirection;
    }

    public boolean canResolveTextDirection() {
        return true;
    }

    public boolean isTextDirectionResolved() {
        return true;
    }

    public int getTextAlignment() {
        return mTextAlignment;
    }

    public void setTextAlignment(int textAlignment) {
        mTextAlignment = textAlignment;
    }

    public int getRawTextAlignment() {
        return mTextAlignment;
    }

    public boolean canResolveTextAlignment() {
        return true;
    }

    public boolean isTextAlignmentResolved() {
        return true;
    }

    public void setLayoutDirection(int layoutDirection) {
        mLayoutDirection = layoutDirection;
    }

    public int getRawLayoutDirection() {
        return mLayoutDirection;
    }

    public boolean canResolveLayoutDirection() {
        return true;
    }

    public boolean isLayoutDirectionResolved() {
        return true;
    }

    public void setAnimation(android.view.animation.Animation animation) {
        mCurrentAnimation = animation;
        if (animation != null) {
            animation.reset();
        }
    }

    // ------------------------------------------------------------------------------------
    // Legacy view animation. The animation is stepped once per frame so its listeners and
    // end state behave; the transformation is not applied to drawing.

    private android.view.animation.Animation mCurrentAnimation;
    private Choreographer.FrameCallback mAnimationTicker;

    public android.view.animation.Animation getAnimation() {
        return mCurrentAnimation;
    }

    public void startAnimation(android.view.animation.Animation animation) {
        animation.setStartTime(android.view.animation.Animation.START_ON_FIRST_FRAME);
        setAnimation(animation);
        invalidate();
        scheduleAnimationTick();
    }

    public void clearAnimation() {
        if (mCurrentAnimation != null) {
            mCurrentAnimation.detach();
        }
        mCurrentAnimation = null;
    }

    protected void onAnimationStart() {
    }

    protected void onAnimationEnd() {
    }

    protected boolean onSetAlpha(int alpha) {
        return false;
    }

    private void scheduleAnimationTick() {
        if (mAnimationTicker != null) {
            return;
        }
        final android.view.animation.Transformation transformation =
                new android.view.animation.Transformation();
        mAnimationTicker = new Choreographer.FrameCallback() {
            @Override
            public void doFrame(long frameTimeNanos) {
                mAnimationTicker = null;
                final android.view.animation.Animation a = mCurrentAnimation;
                if (a == null) {
                    return;
                }
                final long now = frameTimeNanos / 1000000L;
                final boolean more = a.getTransformation(now, transformation);
                if (more) {
                    invalidate();
                    scheduleAnimationTick();
                } else {
                    mCurrentAnimation = null;
                    invalidate();
                }
            }
        };
        Choreographer.getInstance().postFrameCallback(mAnimationTicker);
    }

    // ------------------------------------------------------------------------------------
    // Instance state.

    protected android.os.Parcelable onSaveInstanceState() {
        return BaseSavedState.EMPTY_STATE;
    }

    protected void onRestoreInstanceState(android.os.Parcelable state) {
        if (state != null && state != BaseSavedState.EMPTY_STATE) {
            throw new IllegalArgumentException("Wrong state class, expecting View State but "
                    + "received " + state.getClass().toString() + " instead. This usually happens "
                    + "when two views of different type have the same id in the same hierarchy.");
        }
    }

    public void saveHierarchyState(android.util.SparseArray<android.os.Parcelable> container) {
        dispatchSaveInstanceState(container);
    }

    protected void dispatchSaveInstanceState(android.util.SparseArray<android.os.Parcelable> container) {
        if (mId != NO_ID && mSaveEnabled) {
            final android.os.Parcelable state = onSaveInstanceState();
            if (state != null) {
                container.put(mId, state);
            }
        }
    }

    public void restoreHierarchyState(android.util.SparseArray<android.os.Parcelable> container) {
        dispatchRestoreInstanceState(container);
    }

    protected void dispatchRestoreInstanceState(
            android.util.SparseArray<android.os.Parcelable> container) {
        if (mId != NO_ID) {
            final android.os.Parcelable state = container.get(mId);
            if (state != null) {
                onRestoreInstanceState(state);
            }
        }
    }

    void sendAccessibilityEventInternal(int eventType) {
    }
}
