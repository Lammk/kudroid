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
public class View implements KeyEvent.Callback {
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
        boolean onDrag(View v, Object event);
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
        }

        public boolean performAccessibilityAction(View host, int action, Bundle args) {
            return false;
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
        onLayout(changed, l, t, r, b);
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
        mVisibility = visibility;
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
        return mVisibility == VISIBLE;
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
        mBackground = background;
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

    public static class BaseSavedState {
        public BaseSavedState() {}
    }

    public void scrollTo(int x, int y) {
        if (mScrollX != x || mScrollY != y) {
            mScrollX = x;
            mScrollY = y;
            invalidate();
        }
    }
    public void scrollBy(int x, int y) {
        scrollTo(mScrollX + x, mScrollY + y);
    }

    public interface OnCreateContextMenuListener {
    }

    public interface OnHoverListener {
    }

    public interface OnSystemUiVisibilityChangeListener {
        void onSystemUiVisibilityChange(int visibility);
    }

    public interface OnUnhandledKeyEventListener {
    }

    public void saveAttributeDataForStyleable(Context context, int[] styleable,
            android.util.AttributeSet set, android.content.res.TypedArray attrs,
            int defStyleAttr, int defStyleRes) {
    }

    // State flags. They are stored and reported; dispatching them to drawables comes with
    // drawable support.

    public android.graphics.drawable.Drawable getForeground() { return mForeground; }
    public void setForeground(android.graphics.drawable.Drawable foreground) {
        mForeground = foreground;
        invalidate();
    }

    public boolean isEnabled() { return mEnabled; }
    public void setEnabled(boolean enabled) {
        if (mEnabled == enabled) return;
        mEnabled = enabled;
        invalidate();
    }
    public boolean isSelected() { return mSelected; }
    public void setSelected(boolean selected) {
        if (mSelected == selected) return;
        mSelected = selected;
        invalidate();
    }
    public boolean isPressed() { return mPressed; }
    public void setPressed(boolean pressed) {
        if (mPressed == pressed) return;
        mPressed = pressed;
        invalidate();
    }
    public boolean isActivated() { return mActivated; }
    public void setActivated(boolean activated) {
        if (mActivated == activated) return;
        mActivated = activated;
        invalidate();
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
}
