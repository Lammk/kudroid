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
    private static final class FloatViewProperty extends android.util.FloatProperty<View> {
        private final int mKind;
        FloatViewProperty(String name, int kind) {
            super(name);
            mKind = kind;
        }
        @Override
        public void setValue(View v, float value) {
            switch (mKind) {
                case 0: v.setAlpha(value); break;
                case 1: v.setTranslationX(value); break;
                case 2: v.setTranslationY(value); break;
                case 3: v.setTranslationZ(value); break;
                case 4: v.setX(value); break;
                case 5: v.setY(value); break;
                case 6: v.setZ(value); break;
                case 7: v.setRotation(value); break;
                case 8: v.setRotationX(value); break;
                case 9: v.setRotationY(value); break;
                case 10: v.setScaleX(value); break;
                default: v.setScaleY(value); break;
            }
        }
        @Override
        public Float get(View v) {
            switch (mKind) {
                case 0: return v.getAlpha();
                case 1: return v.getTranslationX();
                case 2: return v.getTranslationY();
                case 3: return v.getTranslationZ();
                case 4: return v.getX();
                case 5: return v.getY();
                case 6: return v.getZ();
                case 7: return v.getRotation();
                case 8: return v.getRotationX();
                case 9: return v.getRotationY();
                case 10: return v.getScaleX();
                default: return v.getScaleY();
            }
        }
    }
    public static final android.util.Property<View, Float> ALPHA = new FloatViewProperty("alpha", 0);
    public static final android.util.Property<View, Float> TRANSLATION_X = new FloatViewProperty("translationX", 1);
    public static final android.util.Property<View, Float> TRANSLATION_Y = new FloatViewProperty("translationY", 2);
    public static final android.util.Property<View, Float> TRANSLATION_Z = new FloatViewProperty("translationZ", 3);
    public static final android.util.Property<View, Float> X = new FloatViewProperty("x", 4);
    public static final android.util.Property<View, Float> Y = new FloatViewProperty("y", 5);
    public static final android.util.Property<View, Float> Z = new FloatViewProperty("z", 6);
    public static final android.util.Property<View, Float> ROTATION = new FloatViewProperty("rotation", 7);
    public static final android.util.Property<View, Float> ROTATION_X = new FloatViewProperty("rotationX", 8);
    public static final android.util.Property<View, Float> ROTATION_Y = new FloatViewProperty("rotationY", 9);
    public static final android.util.Property<View, Float> SCALE_X = new FloatViewProperty("scaleX", 10);
    public static final android.util.Property<View, Float> SCALE_Y = new FloatViewProperty("scaleY", 11);
    /** view visibility: visible. */
    public static final int VISIBLE = 0;
    /** view visibility: invisible. */
    public static final int INVISIBLE = 4;
    /** view visibility: disappears. */
    public static final int GONE = 8;
    static final int[] VISIBILITY_FLAGS = { VISIBLE, INVISIBLE, GONE };
    /** Used to mark a View that has no ID. */
    public static final int NO_ID = -1;
    /** measurement specification mode: not specified. */
    public static final int UNSPECIFIED = 0;
    /** measurement specification mode: exact. */
    public static final int EXACTLY = 0x40000000;
    /** measurement specification mode: most. */
    public static final int AT_MOST = 0x80000000;
    protected final Context mContext;
    int mLeft;
    int mTop;
    int mRight;
    int mBottom;
    int mVisibility = VISIBLE;
    int mId = -1;
    ViewGroup mParent;
    android.os.Handler mRunQueue;
    OnClickListener mOnClickListener;
    OnLongClickListener mOnLongClickListener;
    OnTouchListener mOnTouchListener;
    OnFocusChangeListener mOnFocusChangeListener;
    OnKeyListener mOnKeyListener;
    AccessibilityDelegate mAccessibilityDelegate;
    ViewTreeObserver mViewTreeObserver;
    boolean mHasFocus;
    long mTouchDownTime;
    boolean mLongClickFired;
    boolean mLaidOut;
    boolean mLayoutRequested = true;
    boolean mLayoutRequired;
    boolean mMeasureValid;
    int mOldWidthMeasureSpec;
    int mOldHeightMeasureSpec;
    java.util.ArrayList<OnLayoutChangeListener> mLayoutChangeListeners;
    int mMeasuredWidth;
    int mMeasuredHeight;
    protected int mScrollX = 0;
    protected int mScrollY = 0;
    protected int mPaddingLeft;
    protected int mPaddingTop;
    protected int mPaddingRight;
    protected int mPaddingBottom;
    int mMinWidth;
    int mMinHeight;
    boolean mEnabled = true;
    android.graphics.drawable.Drawable mForeground;
    boolean mClickable;
    boolean mLongClickable;
    boolean mFocusable = true;
    boolean mFocusableInTouchMode;
    boolean mSelected;
    boolean mPressed;
    boolean mActivated;
    float mAlpha = 1.0f;
    float mTranslationX;
    float mTranslationY;
    float mTranslationZ;
    float mElevation;
    float mScaleX = 1.0f;
    float mScaleY = 1.0f;
    float mRotation;
    float mPivotX;
    float mPivotY;
    boolean mPivotSet;
    /** Android long press threshold (ViewConfiguration.getLongPressTimeout). */
    static final long LONG_PRESS_TIMEOUT = 500L;
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
    public void addOnLayoutChangeListener(OnLayoutChangeListener listener) { ViewAttributes.addOnLayoutChangeListener(this, listener); }
    public void removeOnLayoutChangeListener(OnLayoutChangeListener listener) { if (mLayoutChangeListeners != null) mLayoutChangeListeners.remove(listener); }
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
    private void applyViewAttributes(android.content.res.TypedArray a) { ViewAttributes.applyViewAttributes(this, a); }
    // android:onClick="method": resolved against the view's context when first clicked.
    static final class DeclaredOnClickListener implements OnClickListener {
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
    public Context getContext() { return mContext; }
    /**
     * returns the id of the view.
     */
    public int getId() { return mId; }
    /**
     * set the id of the view.
     */
    public void setId(int id) { mId = id; }
    public View findViewById(int id) { return ViewAttributes.findViewById(this, id); }
    public View findViewWithTag(Object tag) { return ViewAttributes.findViewWithTag(this, tag); }
    /** Called after this view and all its children have been inflated from XML. */
    protected void onFinishInflate() { }
    public View getRootView() { return ViewAttributes.getRootView(this); }
    CharSequence mContentDescription;
    public void setContentDescription(CharSequence contentDescription) { mContentDescription = contentDescription; }
    public CharSequence getContentDescription() { return mContentDescription; }
    /**
     * returns the left position of the view.
     */
    public int getLeft() { return mLeft; }
    /**
     * returns the top position of the view.
     */
    public int getTop() { return mTop; }
    /**
     * returns the right position of the view.
     */
    public int getRight() { return mRight; }
    /**
     * returns the bottom position of the view.
     */
    public int getBottom() { return mBottom; }
    /**
     * returns the width of the view.
     */
    public int getWidth() { return mRight - mLeft; }
    /**
     * returns the height of the view.
     */
    public int getHeight() { return mBottom - mTop; }
    /**
     * sets the view's layout boundaries.
     *
     * Android's contract: layout() records the bounds and then hands them to
     * onLayout() so a container can position its children. Only assigning the
     * fields (which is what this used to do) means ViewGroup.onLayout never runs,
     * every child keeps its default 0,0,0,0 bounds, and the whole hierarchy draws
     * on top of itself in the top-left corner.
     */
    public void layout(int l, int t, int r, int b) { ViewAttributes.layout(this, l, t, r, b); }
    /** Notify subclasses of new dimensions before laying out their children. */
    protected void onSizeChanged(int w, int h, int oldw, int oldh) { }
    /**
     * Position child views. Leaf views have nothing to do; ViewGroup overrides this.
     */
    protected void onLayout(boolean changed, int l, int t, int r, int b) { }
    /** True once layout() has run at least once. */
    public boolean isLaidOut() { return mLaidOut; }
    /** True while a layout pass is owed to this view. */
    public boolean isLayoutRequested() { return mLayoutRequested; }
    /**
     * returns the view's visibility.
     */
    public int getVisibility() { return mVisibility; }
    /**
     * set the view's visibility.
     */
    public void setVisibility(int visibility) { ViewAttributes.setVisibility(this, visibility); }
    /**
     * The container this view sits in, or null at the root.
     *
     * Declared as ViewParent, not ViewGroup, because that is the signature guest code
     * references: {@code View.getParent()Landroid/view/ViewParent;}. Returning ViewGroup
     * made that reference resolve to nothing, so an app assigning the result to a
     * ViewParent got a NoSuchMethodError for a method that plainly existed — it was the
     * RETURN TYPE that did not match. Five of six APKs in the corpus need this one.
     */
    public ViewParent getParent() { return mParent; }
    /**
     * The parent as a ViewGroup, for callers inside the framework that need to touch child
     * management. Separate from getParent() so that one can keep Android's signature.
     */
    public ViewGroup getParentGroup() { return mParent; }
    /**
     * set the view's parent.
     */
    public void setParent(ViewGroup parent) { ViewAttributes.setParent(this, parent); }
    /**
     * set up click listener.
     */
    public void setOnClickListener(OnClickListener l) { ViewAttributes.setOnClickListener(this, l); }
    public void setOnLongClickListener(OnLongClickListener l) { ViewAttributes.setOnLongClickListener(this, l); }
    public void setOnTouchListener(OnTouchListener l) { mOnTouchListener = l; }
    public void setOnFocusChangeListener(OnFocusChangeListener l) { mOnFocusChangeListener = l; }
    public void setOnKeyListener(OnKeyListener l) { mOnKeyListener = l; }
    public boolean isClickable() { return mClickable; }
    public void setClickable(boolean clickable) { mClickable = clickable; }
    public boolean isLongClickable() { return mLongClickable; }
    public void setLongClickable(boolean longClickable) { mLongClickable = longClickable; }
    public boolean hasOnClickListeners() { return mOnClickListener != null; }
    public boolean callOnClick() { return performClick(); }
    public boolean hasFocus() { return mHasFocus; }
    public boolean hasWindowFocus() { return true; }
    public boolean isFocused() { return mHasFocus; }
    /** The view that holds focus. One per process, as there is one window. */
    static View sFocusedView;
    /** The view that currently holds focus, or null. */
    protected static View getFocusedView() { return sFocusedView; }
    /** Change focus state and fire callbacks if there is a real change. */
    public void setFocus(boolean hasFocus) { ViewAttributes.setFocus(this, hasFocus); }
    /** Called when this view gains or loses focus. */
    protected void onFocusChanged(boolean gainFocus, int direction, android.graphics.Rect previouslyFocusedRect) { }
    public void clearFocus() { setFocus(false); }
    public boolean dispatchKeyEvent(KeyEvent event) { return ViewAttributes.dispatchKeyEvent(this, event); }
    public boolean onKeyDown(int keyCode, KeyEvent event) { return false; }
    public boolean onKeyLongPress(int keyCode, KeyEvent event) { return false; }
    public boolean onKeyUp(int keyCode, KeyEvent event) { return false; }
    public boolean onKeyMultiple(int keyCode, int count, KeyEvent event) { return false; }
    public KeyEvent.DispatcherState getKeyDispatcherState() { return ViewAttributes.getKeyDispatcherState(this); }
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
    int mSystemUiVisibility = SYSTEM_UI_FLAG_VISIBLE;
    OnSystemUiVisibilityChangeListener mOnSystemUiVisibilityChangeListener;
    public int getSystemUiVisibility() { return mSystemUiVisibility; }
    public void setSystemUiVisibility(int visibility) { ViewAttributes.setSystemUiVisibility(this, visibility); }
    public void setOnSystemUiVisibilityChangeListener(
            OnSystemUiVisibilityChangeListener l) { ViewAttributes.setOnSystemUiVisibilityChangeListener(this, l); }
    public boolean isInEditMode() { return false; }
    // Layout params.
    ViewGroup.LayoutParams mLayoutParams;
    public ViewGroup.LayoutParams getLayoutParams() { return mLayoutParams; }
    public void setLayoutParams(ViewGroup.LayoutParams params) { ViewAttributes.setLayoutParams(this, params); }
    /** Assign layout parameters without scheduling a layout (the parent does that once). */
    void setLayoutParamsNoRequest(ViewGroup.LayoutParams params) { ViewAttributes.setLayoutParamsNoRequest(this, params); }
    public void resolveLayoutParams() { if (mLayoutParams != null) mLayoutParams.resolveLayoutDirection(getLayoutDirection()); }
    public boolean resolveRtlPropertiesIfNeeded() { return ViewAttributes.resolveRtlPropertiesIfNeeded(this); }
    // IME attachment.
    /** Create the InputConnection for an IME, or null when text is not accepted. */
    public android.view.inputmethod.InputConnection onCreateInputConnection(
            android.view.inputmethod.EditorInfo outAttrs) { return ViewAttributes.onCreateInputConnection(this, outAttrs); }
    /** True when this view wants a soft keyboard. */
    public boolean onCheckIsTextEditor() { return false; }
    /**
     * Ask for focus.
     *
     * The IME is shown for the focused view, so this is the hook that decides which
     * InputConnection subsequent text goes to.
     */
    public boolean requestFocus() { return ViewAttributes.requestFocus(this); }
    public boolean isFocusable() { return mFocusable; }
    public void setFocusable(boolean focusable) { mFocusable = focusable; }
    public boolean isFocusableInTouchMode() { return mFocusableInTouchMode; }
    public void setFocusableInTouchMode(boolean focusable) { ViewAttributes.setFocusableInTouchMode(this, focusable); }
    /**
     * The token identifying the window this view is in.
     *
     * Apps pass it to InputMethodManager.hideSoftInputFromWindow. There is one window
     * under KuDroid, so a per-view token would be meaningless; the root view stands in
     * for it, which keeps the identity comparison apps make meaningful.
     */
    public android.os.IBinder getWindowToken() { return null; }
    /**
     * ViewTreeObserver of this view tree (shared with parent if attached).
     */
    public ViewTreeObserver getViewTreeObserver() { return ViewAttributes.getViewTreeObserver(this); }
    public void setAccessibilityDelegate(AccessibilityDelegate delegate) { mAccessibilityDelegate = delegate; }
    public boolean dispatchTouchEvent(MotionEvent event) { return ViewAttributes.dispatchTouchEvent(this, event); }
    public boolean onTouchEvent(MotionEvent event) { return ViewAttributes.onTouchEvent(this, event); }
    /**
     * make one click.
     */
    public boolean performClick() { return ViewAttributes.performClick(this); }
    public boolean performLongClick() { return ViewAttributes.performLongClick(this); }
    /**
     * Measure view. Subclasses must call setMeasuredDimension() before returning.
     * The default reports zero size, matching Android's behaviour for a bare View.
     */
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) { ViewAttributes.onMeasure(this, widthMeasureSpec, heightMeasureSpec); }
    /**
     * measure view (called by parent).
     */
    public void measure(int widthMeasureSpec, int heightMeasureSpec) { ViewAttributes.measure(this, widthMeasureSpec, heightMeasureSpec); }
    /**
     * Record the size decided by onMeasure. Kept separate from mLeft/mRight because
     * measuring happens before positioning: the parent needs to know how big a child
     * wants to be in order to work out where to put it.
     */
    protected final void setMeasuredDimension(int measuredWidth, int measuredHeight) { ViewAttributes.setMeasuredDimension(this, measuredWidth, measuredHeight); }
    public final int getMeasuredWidth() { return mMeasuredWidth & MEASURED_SIZE_MASK; }
    public final int getMeasuredHeight() { return mMeasuredHeight & MEASURED_SIZE_MASK; }
    /**
     * Resolve a measure spec the way Android does: honour an exact or bounded size
     * from the parent, otherwise fall back to the view's own preference.
     */
    public static int getDefaultSize(int size, int measureSpec) { return ViewAttributes.getDefaultSize(size, measureSpec); }
    /**
     * Packs a size and a constraint mode into one int, as Android does, so a parent
     * can tell a child "you get exactly N" or "at most N" in a single argument.
     */
    public static class MeasureSpec {
        public static final int UNSPECIFIED = 0;
        public static final int EXACTLY = 0x40000000;
        public static final int AT_MOST = 0x80000000;
        private static final int MODE_SHIFT = 30;
        private static final int MODE_MASK = 0x3 << MODE_SHIFT;
        public static int makeMeasureSpec(int size, int mode) {
            return (size & ~MODE_MASK) | (mode & MODE_MASK);
        }
        public static int getMode(int measureSpec) {
            return measureSpec & MODE_MASK;
        }
        public static int getSize(int measureSpec) {
            return measureSpec & ~MODE_MASK;
        }
    }
    /**
     * draw views.
     */
    protected void onDraw(Canvas canvas) { }
    protected void onAttachedToWindow() { }
    protected void onDetachedFromWindow() { }
    /**
     * Draws this view in its own coordinate space: (0, 0) is its top-left corner, and the canvas
     * is already offset by the scroll position. Order matches Android: background, content,
     * children, foreground.
     */
    public void draw(Canvas canvas) { ViewDrawing.draw(this, canvas); }
    /** The background does not scroll with the content. */
    private void drawBackground(Canvas canvas) { ViewDrawing.drawBackground(this, canvas); }
    /**
     * Draws this view as a child of {@code parent}: positions the canvas at the view's frame,
     * applies its transform, alpha and clip, then draws it. Returns false (nothing animates).
     */
    boolean draw(Canvas canvas, ViewGroup parent, long drawingTime) { return ViewDrawing.draw(this, canvas, parent, drawingTime); }
    /**
     * returns the display state of the view.
     */
    public boolean isShown() { return ViewDrawing.isShown(this); }
    android.graphics.drawable.Drawable mBackground;
    /**
     * returns the background of the view.
     */
    public android.graphics.drawable.Drawable getBackground() { return mBackground; }
    /**
     * The modern name for setBackgroundDrawable, and the one apps use.
     *
     * All five real APKs in the corpus reference setBackground; only the deprecated spelling
     * existed. Stored rather than dropped so getBackground() returns what was set — a view
     * that accepts a background and reports null makes any app that reads it back believe it
     * has none.
     */
    public void setBackground(android.graphics.drawable.Drawable background) { setBackgroundDrawable(background); }
    /** Stores the background; subclasses override this spelling and call super. */
    public void setBackgroundDrawable(android.graphics.drawable.Drawable background) { ViewDrawing.setBackgroundDrawable(this, background); }
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
    public void getLocationOnScreen(int[] location) { ViewDrawing.getLocationOnScreen(this, location); }
    public void getLocationInWindow(int[] location) { getLocationOnScreen(location); }
    /**
     * The Resources of this view's Context.
     *
     * Needed by all five corpus APKs: this is how view code reads dimensions and densities
     * without being passed a Context. Falls back to the system Resources when the view has
     * no context, because callers chain straight off it — getResources().getDisplayMetrics()
     * is the common form, and a null here is an NPE one frame later.
     */
    public android.content.res.Resources getResources() { return ViewDrawing.getResources(this); }
    public Display getDisplay() { return new Display(); }
    /**
     * set the background of the view.
     */
    public void setBackgroundColor(int color) { ViewDrawing.setBackgroundColor(this, color); }
    /**
     * sets the view's padding.
     */
    public void setPadding(int left, int top, int right, int bottom) { ViewDrawing.setPadding(this, left, top, right, bottom); }
    public void setPaddingRelative(int start, int top, int end, int bottom) { setPadding(start, top, end, bottom); }
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
    public void invalidate() { ViewDrawing.invalidate(this); }
    /**
     * Ask for another measure + layout pass. Same top-down model as invalidate().
     */
    public void requestLayout() { ViewDrawing.requestLayout(this); }
    private synchronized android.os.Handler getRunQueue() { return ViewDrawing.getRunQueue(this); }
    /**
     * Enqueue work for the UI thread. Posted actions never run inline.
     */
    public boolean post(Runnable action) { return ViewDrawing.post(this, action); }
    /** Enqueue work for the UI thread after the requested delay. */
    public boolean postDelayed(Runnable action, long delayMillis) { return ViewDrawing.postDelayed(this, action, delayMillis); }
    /** Remove queued instances of a Runnable posted through this View. */
    public boolean removeCallbacks(Runnable action) { return ViewDrawing.removeCallbacks(this, action); }
    public final void cancelPendingInputEvents() { dispatchCancelPendingInputEvents(); }
    protected void dispatchCancelPendingInputEvents() { onCancelPendingInputEvents(); }
    public void onCancelPendingInputEvents() { ViewDrawing.onCancelPendingInputEvents(this); }
    // Tags.
    Object mTag;
    android.util.SparseArray<Object> mKeyedTags;
    public Object getTag() { return mTag; }
    public void setTag(Object tag) { mTag = tag; }
    public Object getTag(int key) { return mKeyedTags != null ? mKeyedTags.get(key) : null; }
    /**
     * Attach data under an integer key.
     *
     * Android requires the key to be an application-declared resource id, and throws
     * IllegalArgumentException otherwise, to stop two libraries colliding on the same
     * arbitrary constant. KuDroid has no resource ids to validate against, so the key
     * is accepted as given: rejecting valid-but-unverifiable keys would break the very
     * libraries this exists for.
     */
    public void setTag(int key, Object tag) { ViewDrawing.setTag(this, key, tag); }
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
    static final java.util.concurrent.atomic.AtomicInteger sNextGeneratedId =
            new java.util.concurrent.atomic.AtomicInteger(1);
    public static int generateViewId() { return ViewDrawing.generateViewId(); }
    // Window insets.
    OnApplyWindowInsetsListener mOnApplyWindowInsetsListener;
    boolean mFitsSystemWindows = false;
    /** Install a listener that consumes window insets before this view does. */
    public void setOnApplyWindowInsetsListener(OnApplyWindowInsetsListener listener) { mOnApplyWindowInsetsListener = listener; }
    public OnApplyWindowInsetsListener getOnApplyWindowInsetsListener() { return mOnApplyWindowInsetsListener; }
    public void requestApplyInsets() { requestFitSystemWindows(); }
    public void requestFitSystemWindows() { ViewDrawing.requestFitSystemWindows(this); }
    public WindowInsets onApplyWindowInsets(WindowInsets insets) { return ViewDrawing.onApplyWindowInsets(this, insets); }
    protected boolean fitSystemWindows(android.graphics.Rect insets) { return ViewDrawing.fitSystemWindows(this, insets); }
    public WindowInsets dispatchApplyWindowInsets(WindowInsets insets) { return ViewDrawing.dispatchApplyWindowInsets(this, insets); }
    public WindowInsets getRootWindowInsets() { return ViewDrawing.getRootWindowInsets(this); }
    public WindowInsets computeSystemWindowInsets(WindowInsets in, android.graphics.Rect outLocalInsets) { return ViewDrawing.computeSystemWindowInsets(this, in, outLocalInsets); }
    public boolean fitsSystemWindows() { return mFitsSystemWindows; }
    public void setFitsSystemWindows(boolean fitSystemWindows) { ViewDrawing.setFitsSystemWindows(this, fitSystemWindows); }
    public boolean getFitsSystemWindows() { return mFitsSystemWindows; }
    boolean mKeepScreenOn = false;
    private static native void setKeepScreenOnNative(boolean keepOn);
    public void setKeepScreenOn(boolean keepScreenOn) { ViewDrawing.setKeepScreenOn(this, keepScreenOn); }
    public boolean getKeepScreenOn() { return mKeepScreenOn; }
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
    public void scrollTo(int x, int y) { ViewDrawing.scrollTo(this, x, y); }
    public void scrollBy(int x, int y) { scrollTo(mScrollX + x, mScrollY + y); }
    public interface OnSystemUiVisibilityChangeListener {
        void onSystemUiVisibilityChange(int visibility);
    }
    public void saveAttributeDataForStyleable(Context context, int[] styleable,
            android.util.AttributeSet set, android.content.res.TypedArray attrs,
            int defStyleAttr, int defStyleRes) { ViewDrawing.saveAttributeDataForStyleable(this, context, styleable, set, attrs, defStyleAttr, defStyleRes); }
    // State flags. They are stored and reported; dispatching them to drawables comes with
    // drawable support.
    public android.graphics.drawable.Drawable getForeground() { return mForeground; }
    public void setForeground(android.graphics.drawable.Drawable foreground) { ViewDrawing.setForeground(this, foreground); }
    public boolean isEnabled() { return mEnabled; }
    public void setEnabled(boolean enabled) { ViewDrawing.setEnabled(this, enabled); }
    public boolean isSelected() { return mSelected; }
    public void setSelected(boolean selected) { ViewDrawing.setSelected(this, selected); }
    protected void dispatchSetSelected(boolean selected) { }
    public boolean isPressed() { return mPressed; }
    public void setPressed(boolean pressed) { ViewDrawing.setPressed(this, pressed); }
    protected void dispatchSetPressed(boolean pressed) { }
    public boolean isActivated() { return mActivated; }
    public void setActivated(boolean activated) { ViewDrawing.setActivated(this, activated); }
    protected void dispatchSetActivated(boolean activated) { }
    // Property transforms are stored for getters and animators; drawing does not apply them.
    ViewOverlay mOverlay;
    /** Drawables added here are kept for the overlay; they are not drawn. */
    public ViewOverlay getOverlay() { return ViewDrawing.getOverlay(this); }
    ViewPropertyAnimator mAnimator;
    /** The animator for this view's properties; created on first use. */
    public ViewPropertyAnimator animate() { return ViewDrawing.animate(this); }
    public float getAlpha() { return mAlpha; }
    public void setAlpha(float alpha) { ViewDrawing.setAlpha(this, alpha); }
    public float getTranslationX() { return mTranslationX; }
    public void setTranslationX(float v) { ViewDrawing.setTranslationX(this, v); }
    public float getTranslationY() { return mTranslationY; }
    public void setTranslationY(float v) { ViewDrawing.setTranslationY(this, v); }
    public float getTranslationZ() { return mTranslationZ; }
    public void setTranslationZ(float v) { ViewDrawing.setTranslationZ(this, v); }
    public float getElevation() { return mElevation; }
    public void setElevation(float v) { mElevation = v; }
    public float getScaleX() { return mScaleX; }
    public void setScaleX(float v) { ViewDrawing.setScaleX(this, v); }
    public float getScaleY() { return mScaleY; }
    public void setScaleY(float v) { ViewDrawing.setScaleY(this, v); }
    public float getRotation() { return mRotation; }
    public void setRotation(float v) { ViewDrawing.setRotation(this, v); }
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
    public void setMinimumWidth(int minWidth) { ViewDrawing.setMinimumWidth(this, minWidth); }
    public void setMinimumHeight(int minHeight) { ViewDrawing.setMinimumHeight(this, minHeight); }
    protected int getSuggestedMinimumWidth() { return mBackground == null ? mMinWidth : Math.max(mMinWidth, mBackground.getMinimumWidth()); }
    protected int getSuggestedMinimumHeight() { return mBackground == null ? mMinHeight : Math.max(mMinHeight, mBackground.getMinimumHeight()); }
    public static int resolveSize(int size, int measureSpec) { return resolveSizeAndState(size, measureSpec, 0) & MEASURED_SIZE_MASK; }
    public static int resolveSizeAndState(int size, int measureSpec, int childMeasuredState) { return ViewDrawing.resolveSizeAndState(size, measureSpec, childMeasuredState); }
    public static int combineMeasuredStates(int curState, int newState) { return curState | newState; }
    public final int getMeasuredState() { return ViewDrawing.getMeasuredState(this); }
    /** Mark this view as needing layout without asking ancestors. */
    public void forceLayout() { ViewGeometry.forceLayout(this); }
    // Posting.
    public void postInvalidate() { post(new Runnable() { public void run() { invalidate(); } }); }
    public void postOnAnimation(Runnable action) { post(action); }
    public void postOnAnimationDelayed(Runnable action, long delayMillis) { postDelayed(action, delayMillis); }
    // Layout direction: left-to-right only.
    public static final int LAYOUT_DIRECTION_LTR = 0;
    public static final int LAYOUT_DIRECTION_RTL = 1;
    public int getLayoutDirection() { return ViewGeometry.getLayoutDirection(this); }
    public long getDrawingTime() { return ViewGeometry.getDrawingTime(this); }
    public boolean isLayoutRtl() { return false; }
    public void setBackgroundResource(int resid) { setBackground(resid == 0 ? null : getContext().getResources().getDrawable(resid, getContext().getTheme())); }
    // ------------------------------------------------------------------------------------
    // Attach state, visibility and window callbacks.
    boolean mAttachedToWindow;
    Window mTraversalWindow;
    void setTraversalWindow(Window window) { mTraversalWindow = window; }
    int mWindowAttachCount;
    java.util.concurrent.CopyOnWriteArrayList<OnAttachStateChangeListener>
            mAttachStateListeners;
    boolean mTemporarilyDetached;
    public boolean isAttachedToWindow() { return mAttachedToWindow; }
    protected int getWindowAttachCount() { return mWindowAttachCount; }
    public void addOnAttachStateChangeListener(OnAttachStateChangeListener listener) { ViewGeometry.addOnAttachStateChangeListener(this, listener); }
    public void removeOnAttachStateChangeListener(OnAttachStateChangeListener listener) { if (mAttachStateListeners != null) mAttachStateListeners.remove(listener); }
    /** Called when the hierarchy this view belongs to becomes part of a window. */
    void dispatchAttachedToWindow() { ViewGeometry.dispatchAttachedToWindow(this); }
    void dispatchDetachedFromWindow() { ViewGeometry.dispatchDetachedFromWindow(this); }
    /** Called when the visibility of this view or any ancestor changed. */
    public void onVisibilityAggregated(boolean isVisible) { ViewGeometry.onVisibilityAggregated(this, isVisible); }
    boolean dispatchVisibilityAggregated(boolean isVisible) { return ViewGeometry.dispatchVisibilityAggregated(this, isVisible); }
    protected void dispatchVisibilityChanged(View changedView, int visibility) { onVisibilityChanged(changedView, visibility); }
    protected void onVisibilityChanged(View changedView, int visibility) { }
    public void dispatchWindowVisibilityChanged(int visibility) { onWindowVisibilityChanged(visibility); }
    protected void onWindowVisibilityChanged(int visibility) { }
    public int getWindowVisibility() { return VISIBLE; }
    public void dispatchWindowFocusChanged(boolean hasFocus) { onWindowFocusChanged(hasFocus); }
    public void onWindowFocusChanged(boolean hasWindowFocus) { }
    public void dispatchConfigurationChanged(android.content.res.Configuration newConfig) { onConfigurationChanged(newConfig); }
    protected void onConfigurationChanged(android.content.res.Configuration newConfig) { }
    public void dispatchDisplayHint(int hint) { onDisplayHint(hint); }
    protected void onDisplayHint(int hint) { }
    public void dispatchSystemUiVisibilityChanged(int visibility) { ViewGeometry.dispatchSystemUiVisibilityChanged(this, visibility); }
    public void dispatchWindowSystemUiVisiblityChanged(int visible) { onWindowSystemUiVisibilityChanged(visible); }
    public void onWindowSystemUiVisibilityChanged(int visible) { }
    public int getWindowSystemUiVisibility() { return 0; }
    public void onScreenStateChanged(int screenState) { }
    public void onRtlPropertiesChanged(int layoutDirection) { }
    public android.os.IBinder getApplicationWindowToken() { return null; }
    public android.os.Handler getHandler() { return getRunQueue(); }
    public boolean isInTouchMode() { return true; }
    public boolean isHardwareAccelerated() { return false; }
    public void dispatchStartTemporaryDetach() { ViewGeometry.dispatchStartTemporaryDetach(this); }
    public void dispatchFinishTemporaryDetach() { ViewGeometry.dispatchFinishTemporaryDetach(this); }
    public void onStartTemporaryDetach() { }
    public void onFinishTemporaryDetach() { }
    public boolean isTemporarilyDetached() { return mTemporarilyDetached; }
    // ------------------------------------------------------------------------------------
    // View flags that only need to be remembered.
    boolean mWillNotDraw;
    boolean mWillNotCacheDrawing;
    boolean mSaveEnabled = true;
    boolean mSaveFromParentEnabled = true;
    boolean mDuplicateParentState;
    boolean mSoundEffectsEnabled = true;
    boolean mHapticFeedbackEnabled = true;
    boolean mHasTransientState;
    boolean mContextClickable;
    boolean mHovered;
    boolean mScreenReaderFocusable;
    boolean mForceDarkAllowed = true;
    boolean mDefaultFocusHighlightEnabled = true;
    boolean mFocusedByDefault;
    boolean mKeyboardNavigationCluster;
    boolean mRevealOnFocusHint = true;
    boolean mFilterTouchesWhenObscured;
    boolean mScrollContainer;
    boolean mHasOverlappingRendering = true;
    boolean mDrawingCacheEnabled;
    boolean mInLayout;
    int mLayerType = LAYER_TYPE_NONE;
    int mDrawingCacheQuality;
    int mDrawingCacheBackgroundColor;
    CharSequence mTooltipText;
    String mTransitionName;
    float mTransitionAlpha = 1f;
    int mLabelFor = NO_ID;
    TouchDelegate mTouchDelegate;
    android.graphics.Rect mClipBounds;
    boolean mClipToOutline;
    ViewOutlineProvider mOutlineProvider = ViewOutlineProvider.BACKGROUND;
    android.animation.StateListAnimator mStateListAnimator;
    android.graphics.drawable.Drawable mScrollIndicatorDrawable;
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
    public void setWillNotDraw(boolean willNotDraw) { mWillNotDraw = willNotDraw; }
    public boolean willNotDraw() { return mWillNotDraw; }
    public void setWillNotCacheDrawing(boolean willNotCacheDrawing) { mWillNotCacheDrawing = willNotCacheDrawing; }
    public boolean willNotCacheDrawing() { return mWillNotCacheDrawing; }
    public boolean isSaveEnabled() { return mSaveEnabled; }
    public void setSaveEnabled(boolean enabled) { mSaveEnabled = enabled; }
    public boolean isSaveFromParentEnabled() { return mSaveFromParentEnabled; }
    public void setSaveFromParentEnabled(boolean enabled) { mSaveFromParentEnabled = enabled; }
    public boolean isDuplicateParentStateEnabled() { return mDuplicateParentState; }
    public void setDuplicateParentStateEnabled(boolean enabled) { mDuplicateParentState = enabled; }
    public boolean isSoundEffectsEnabled() { return mSoundEffectsEnabled; }
    public void setSoundEffectsEnabled(boolean soundEffectsEnabled) { mSoundEffectsEnabled = soundEffectsEnabled; }
    public boolean isHapticFeedbackEnabled() { return mHapticFeedbackEnabled; }
    public void setHapticFeedbackEnabled(boolean hapticFeedbackEnabled) { mHapticFeedbackEnabled = hapticFeedbackEnabled; }
    public void playSoundEffect(int soundConstant) { }
    public boolean performHapticFeedback(int feedbackConstant) { return false; }
    public boolean performHapticFeedback(int feedbackConstant, int flags) { return false; }
    public boolean hasTransientState() { return mHasTransientState; }
    public void setHasTransientState(boolean hasTransientState) { ViewGeometry.setHasTransientState(this, hasTransientState); }
    public boolean isContextClickable() { return mContextClickable; }
    public void setContextClickable(boolean contextClickable) { mContextClickable = contextClickable; }
    public boolean isHovered() { return mHovered; }
    public void setHovered(boolean hovered) { ViewGeometry.setHovered(this, hovered); }
    public void onHoverChanged(boolean hovered) { }
    public boolean isScreenReaderFocusable() { return mScreenReaderFocusable; }
    public void setScreenReaderFocusable(boolean screenReaderFocusable) { mScreenReaderFocusable = screenReaderFocusable; }
    public boolean isForceDarkAllowed() { return mForceDarkAllowed; }
    public void setForceDarkAllowed(boolean allow) { mForceDarkAllowed = allow; }
    public boolean getDefaultFocusHighlightEnabled() { return mDefaultFocusHighlightEnabled; }
    public void setDefaultFocusHighlightEnabled(boolean enabled) { mDefaultFocusHighlightEnabled = enabled; }
    public boolean isFocusedByDefault() { return mFocusedByDefault; }
    public void setFocusedByDefault(boolean isFocusedByDefault) { mFocusedByDefault = isFocusedByDefault; }
    public boolean isKeyboardNavigationCluster() { return mKeyboardNavigationCluster; }
    public void setKeyboardNavigationCluster(boolean isCluster) { mKeyboardNavigationCluster = isCluster; }
    public boolean getRevealOnFocusHint() { return mRevealOnFocusHint; }
    public void setRevealOnFocusHint(boolean revealOnFocus) { mRevealOnFocusHint = revealOnFocus; }
    public boolean getFilterTouchesWhenObscured() { return mFilterTouchesWhenObscured; }
    public void setFilterTouchesWhenObscured(boolean enabled) { mFilterTouchesWhenObscured = enabled; }
    public boolean onFilterTouchEventForSecurity(MotionEvent event) { return true; }
    public boolean isScrollContainer() { return mScrollContainer; }
    public void setScrollContainer(boolean isScrollContainer) { mScrollContainer = isScrollContainer; }
    public boolean hasOverlappingRendering() { return mHasOverlappingRendering; }
    public boolean getHasOverlappingRendering() { return hasOverlappingRendering(); }
    public void forceHasOverlappingRendering(boolean hasOverlappingRendering) { mHasOverlappingRendering = hasOverlappingRendering; }
    public boolean isOpaque() { return ViewGeometry.isOpaque(this); }
    public boolean isInLayout() { return mInLayout; }
    public boolean isDirty() { return false; }
    public int getLayerType() { return mLayerType; }
    public void setLayerType(int layerType, android.graphics.Paint paint) { ViewGeometry.setLayerType(this, layerType, paint); }
    public void setLayerPaint(android.graphics.Paint paint) { invalidate(); }
    public void buildLayer() { }
    public boolean isDrawingCacheEnabled() { return mDrawingCacheEnabled; }
    public void setDrawingCacheEnabled(boolean enabled) { mDrawingCacheEnabled = enabled; }
    public android.graphics.Bitmap getDrawingCache() { return null; }
    public android.graphics.Bitmap getDrawingCache(boolean autoScale) { return null; }
    public void buildDrawingCache() { }
    public void buildDrawingCache(boolean autoScale) { }
    public void destroyDrawingCache() { }
    public int getDrawingCacheQuality() { return mDrawingCacheQuality; }
    public void setDrawingCacheQuality(int quality) { mDrawingCacheQuality = quality; }
    public int getDrawingCacheBackgroundColor() { return mDrawingCacheBackgroundColor; }
    public void setDrawingCacheBackgroundColor(int color) { mDrawingCacheBackgroundColor = color; }
    public CharSequence getTooltipText() { return mTooltipText; }
    public void setTooltipText(CharSequence tooltipText) { mTooltipText = tooltipText; }
    public String getTransitionName() { return mTransitionName; }
    public void setTransitionName(String transitionName) { mTransitionName = transitionName; }
    public float getTransitionAlpha() { return mTransitionAlpha; }
    public void setTransitionAlpha(float alpha) { ViewGeometry.setTransitionAlpha(this, alpha); }
    public void setTransitionVisibility(int visibility) { mVisibility = visibility; }
    public int getLabelFor() { return mLabelFor; }
    public void setLabelFor(int id) { mLabelFor = id; }
    public TouchDelegate getTouchDelegate() { return mTouchDelegate; }
    public void setTouchDelegate(TouchDelegate delegate) { mTouchDelegate = delegate; }
    public android.graphics.Rect getClipBounds() { return mClipBounds != null ? new android.graphics.Rect(mClipBounds) : null; }
    public boolean getClipBounds(android.graphics.Rect outRect) { return ViewGeometry.getClipBounds(this, outRect); }
    public void setClipBounds(android.graphics.Rect clipBounds) { ViewGeometry.setClipBounds(this, clipBounds); }
    public boolean getClipToOutline() { return mClipToOutline; }
    public void setClipToOutline(boolean clipToOutline) { mClipToOutline = clipToOutline; }
    public ViewOutlineProvider getOutlineProvider() { return mOutlineProvider; }
    public void setOutlineProvider(ViewOutlineProvider provider) { mOutlineProvider = provider; }
    public void invalidateOutline() { }
    public void setOutlineAmbientShadowColor(int color) { }
    public void setOutlineSpotShadowColor(int color) { }
    public android.animation.StateListAnimator getStateListAnimator() { return mStateListAnimator; }
    public void setStateListAnimator(android.animation.StateListAnimator stateListAnimator) { ViewGeometry.setStateListAnimator(this, stateListAnimator); }
    // ------------------------------------------------------------------------------------
    // Layout bounds, position and transforms.
    public void setLeft(int left) { setLeftTopRightBottom(left, mTop, mRight, mBottom); }
    public void setTop(int top) { setLeftTopRightBottom(mLeft, top, mRight, mBottom); }
    public void setRight(int right) { setLeftTopRightBottom(mLeft, mTop, right, mBottom); }
    public void setBottom(int bottom) { setLeftTopRightBottom(mLeft, mTop, mRight, bottom); }
    /** Moves the view's edges without a layout pass; used by animators. */
    public final void setLeftTopRightBottom(int left, int top, int right, int bottom) { ViewGeometry.setLeftTopRightBottom(this, left, top, right, bottom); }
    public void offsetLeftAndRight(int offset) { ViewGeometry.offsetLeftAndRight(this, offset); }
    public void offsetTopAndBottom(int offset) { ViewGeometry.offsetTopAndBottom(this, offset); }
    public void setX(float x) { setTranslationX(x - mLeft); }
    public void setY(float y) { setTranslationY(y - mTop); }
    public float getZ() { return getElevation() + getTranslationZ(); }
    public void setZ(float z) { setTranslationZ(z - getElevation()); }
    float mRotationX;
    float mRotationY;
    float mCameraDistance;
    android.graphics.Matrix mAnimationMatrix;
    public float getRotationX() { return mRotationX; }
    public void setRotationX(float rotationX) { ViewGeometry.setRotationX(this, rotationX); }
    public float getRotationY() { return mRotationY; }
    public void setRotationY(float rotationY) { ViewGeometry.setRotationY(this, rotationY); }
    public float getCameraDistance() { return mCameraDistance; }
    public void setCameraDistance(float distance) { mCameraDistance = distance; }
    public boolean isPivotSet() { return mPivotSet; }
    public void resetPivot() { mPivotSet = false; }
    public android.graphics.Matrix getAnimationMatrix() { return mAnimationMatrix; }
    public void setAnimationMatrix(android.graphics.Matrix matrix) { ViewGeometry.setAnimationMatrix(this, matrix); }
    /** The view's own transform: scale and rotate about the pivot, then translate. */
    public android.graphics.Matrix getMatrix() { return ViewGeometry.getMatrix(this); }
    public boolean hasIdentityMatrix() { return ViewGeometry.hasIdentityMatrix(this); }
    public void transformMatrixToGlobal(android.graphics.Matrix matrix) { ViewGeometry.transformMatrixToGlobal(this, matrix); }
    public void transformMatrixToLocal(android.graphics.Matrix matrix) { ViewGeometry.transformMatrixToLocal(this, matrix); }
    public void getHitRect(android.graphics.Rect outRect) { outRect.set(mLeft, mTop, mRight, mBottom); }
    public void getDrawingRect(android.graphics.Rect outRect) { ViewGeometry.getDrawingRect(this, outRect); }
    public void getFocusedRect(android.graphics.Rect r) { getDrawingRect(r); }
    public boolean getLocalVisibleRect(android.graphics.Rect r) { return ViewGeometry.getLocalVisibleRect(this, r); }
    public boolean getGlobalVisibleRect(android.graphics.Rect r) { return getGlobalVisibleRect(r, null); }
    public boolean getGlobalVisibleRect(android.graphics.Rect r, android.graphics.Point globalOffset) { return ViewGeometry.getGlobalVisibleRect(this, r, globalOffset); }
    public void getWindowVisibleDisplayFrame(android.graphics.Rect outRect) { ViewGeometry.getWindowVisibleDisplayFrame(this, outRect); }
    public void getLocationInSurface(int[] location) { getLocationInWindow(location); }
    public int getMeasuredWidthAndState() { return mMeasuredWidth; }
    public int getMeasuredHeightAndState() { return mMeasuredHeight; }
    public int getBaseline() { return -1; }
    public boolean isPaddingRelative() { return false; }
    protected boolean isPaddingOffsetRequired() { return false; }
    protected int getLeftPaddingOffset() { return 0; }
    protected int getRightPaddingOffset() { return 0; }
    protected int getTopPaddingOffset() { return 0; }
    protected int getBottomPaddingOffset() { return 0; }
    public void bringToFront() { ViewGeometry.bringToFront(this); }
    public static View inflate(Context context, int resource, ViewGroup root) { return LayoutInflater.from(context).inflate(resource, root); }
    public final <T extends View> T requireViewById(int id) { return ViewGeometry.requireViewById(this, id); }
    @Override
    public String toString() { return ViewGeometry.toString(this); }
    // ------------------------------------------------------------------------------------
    // Scrolling.
    int mOverScrollMode = OVER_SCROLL_ALWAYS;
    int mScrollBarStyle = SCROLLBARS_INSIDE_OVERLAY;
    int mScrollBarSize;
    int mScrollBarDefaultDelayBeforeFade = 300;
    int mScrollBarFadeDuration = 250;
    int mVerticalScrollbarPosition = SCROLLBAR_POSITION_DEFAULT;
    int mScrollIndicators;
    int mFadingEdgeLength;
    boolean mHorizontalScrollBarEnabled;
    boolean mVerticalScrollBarEnabled;
    boolean mHorizontalFadingEdgeEnabled;
    boolean mVerticalFadingEdgeEnabled;
    boolean mScrollbarFadingEnabled = true;
    OnScrollChangeListener mOnScrollChangeListener;
    public interface OnScrollChangeListener {
        void onScrollChange(View v, int scrollX, int scrollY, int oldScrollX, int oldScrollY);
    }
    public void setOnScrollChangeListener(OnScrollChangeListener l) { mOnScrollChangeListener = l; }
    public final int getScrollX() { return mScrollX; }
    public final int getScrollY() { return mScrollY; }
    public void setScrollX(int value) { scrollTo(value, mScrollY); }
    public void setScrollY(int value) { scrollTo(mScrollX, value); }
    protected void onScrollChanged(int l, int t, int oldl, int oldt) { ViewGeometry.onScrollChanged(this, l, t, oldl, oldt); }
    public void computeScroll() { }
    protected int computeHorizontalScrollRange() { return getWidth(); }
    protected int computeHorizontalScrollOffset() { return mScrollX; }
    protected int computeHorizontalScrollExtent() { return getWidth(); }
    protected int computeVerticalScrollRange() { return getHeight(); }
    protected int computeVerticalScrollOffset() { return mScrollY; }
    protected int computeVerticalScrollExtent() { return getHeight(); }
    public boolean canScrollHorizontally(int direction) { return ViewGeometry.canScrollHorizontally(this, direction); }
    public boolean canScrollVertically(int direction) { return ViewGeometry.canScrollVertically(this, direction); }
    protected boolean awakenScrollBars() { return false; }
    protected boolean awakenScrollBars(int startDelay) { return false; }
    protected boolean awakenScrollBars(int startDelay, boolean invalidate) { return false; }
    protected void onOverScrolled(int scrollX, int scrollY, boolean clampedX, boolean clampedY) { }
    protected boolean overScrollBy(int deltaX, int deltaY, int scrollX, int scrollY,
            int scrollRangeX, int scrollRangeY, int maxOverScrollX, int maxOverScrollY,
            boolean isTouchEvent) { return ViewGeometry.overScrollBy(this, deltaX, deltaY, scrollX, scrollY, scrollRangeX, scrollRangeY, maxOverScrollX, maxOverScrollY, isTouchEvent); }
    public int getOverScrollMode() { return mOverScrollMode; }
    public void setOverScrollMode(int overScrollMode) { ViewGeometry.setOverScrollMode(this, overScrollMode); }
    public boolean isHorizontalScrollBarEnabled() { return mHorizontalScrollBarEnabled; }
    public void setHorizontalScrollBarEnabled(boolean enabled) { mHorizontalScrollBarEnabled = enabled; }
    public boolean isVerticalScrollBarEnabled() { return mVerticalScrollBarEnabled; }
    public void setVerticalScrollBarEnabled(boolean enabled) { mVerticalScrollBarEnabled = enabled; }
    public boolean isHorizontalFadingEdgeEnabled() { return mHorizontalFadingEdgeEnabled; }
    public void setHorizontalFadingEdgeEnabled(boolean enabled) { mHorizontalFadingEdgeEnabled = enabled; }
    public boolean isVerticalFadingEdgeEnabled() { return mVerticalFadingEdgeEnabled; }
    public void setVerticalFadingEdgeEnabled(boolean enabled) { mVerticalFadingEdgeEnabled = enabled; }
    public int getVerticalFadingEdgeLength() { return mFadingEdgeLength; }
    public int getHorizontalFadingEdgeLength() { return mFadingEdgeLength; }
    public void setFadingEdgeLength(int length) { mFadingEdgeLength = length; }
    protected float getTopFadingEdgeStrength() { return computeVerticalScrollOffset() > 0 ? 1.0f : 0.0f; }
    protected float getBottomFadingEdgeStrength() { return ViewGeometry.getBottomFadingEdgeStrength(this); }
    protected float getLeftFadingEdgeStrength() { return computeHorizontalScrollOffset() > 0 ? 1.0f : 0.0f; }
    protected float getRightFadingEdgeStrength() { return ViewGeometry.getRightFadingEdgeStrength(this); }
    public int getScrollBarStyle() { return mScrollBarStyle; }
    public void setScrollBarStyle(int style) { mScrollBarStyle = style; }
    public int getScrollBarSize() { return mScrollBarSize; }
    public void setScrollBarSize(int scrollBarSize) { mScrollBarSize = scrollBarSize; }
    public int getScrollBarDefaultDelayBeforeFade() { return mScrollBarDefaultDelayBeforeFade; }
    public void setScrollBarDefaultDelayBeforeFade(int delay) { mScrollBarDefaultDelayBeforeFade = delay; }
    public int getScrollBarFadeDuration() { return mScrollBarFadeDuration; }
    public void setScrollBarFadeDuration(int duration) { mScrollBarFadeDuration = duration; }
    public boolean isScrollbarFadingEnabled() { return mScrollbarFadingEnabled; }
    public void setScrollbarFadingEnabled(boolean fadeScrollbars) { mScrollbarFadingEnabled = fadeScrollbars; }
    public int getVerticalScrollbarPosition() { return mVerticalScrollbarPosition; }
    public void setVerticalScrollbarPosition(int position) { mVerticalScrollbarPosition = position; }
    protected int getVerticalScrollbarWidth() { return mScrollBarSize; }
    protected int getHorizontalScrollbarHeight() { return mScrollBarSize; }
    public void setVerticalScrollbarThumbDrawable(android.graphics.drawable.Drawable drawable) { }
    public void setVerticalScrollbarTrackDrawable(android.graphics.drawable.Drawable drawable) { }
    public void setHorizontalScrollbarThumbDrawable(android.graphics.drawable.Drawable drawable) { }
    public void setHorizontalScrollbarTrackDrawable(android.graphics.drawable.Drawable drawable) { }
    protected void initializeScrollbars(android.content.res.TypedArray a) { }
    protected void initializeFadingEdge(android.content.res.TypedArray a) { }
    public int getScrollIndicators() { return mScrollIndicators; }
    public void setScrollIndicators(int indicators) { mScrollIndicators = indicators; }
    public void setScrollIndicators(int indicators, int mask) { mScrollIndicators = (mScrollIndicators & ~mask) | (indicators & mask); }
    protected void onDrawScrollBars(Canvas canvas) { }
    public void onDrawForeground(Canvas canvas) { ViewGeometry.onDrawForeground(this, canvas); }
    protected void dispatchDraw(Canvas canvas) { }
    // ------------------------------------------------------------------------------------
    // Invalidation.
    public void invalidate(android.graphics.Rect dirty) { invalidate(); }
    public void invalidate(int l, int t, int r, int b) { invalidate(); }
    public void postInvalidate(int left, int top, int right, int bottom) { postInvalidate(); }
    public void postInvalidateDelayed(long delayMilliseconds) { postDelayed(new Runnable() { public void run() { invalidate(); } }, delayMilliseconds); }
    public void postInvalidateDelayed(long delayMilliseconds, int left, int top, int right,
            int bottom) { ViewGeometry.postInvalidateDelayed(this, delayMilliseconds, left, top, right, bottom); }
    public void postInvalidateOnAnimation() { postInvalidate(); }
    public void postInvalidateOnAnimation(int left, int top, int right, int bottom) { postInvalidate(); }
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
    int[] mDrawableState;
    /** The current state as drawable state attribute ids, rebuilt after refreshDrawableState. */
    public final int[] getDrawableState() { return ViewInteraction.getDrawableState(this); }
    /**
     * Builds the state array. extraSpace trailing zeros are left for subclasses, which fill them
     * through mergeDrawableStates.
     */
    protected int[] onCreateDrawableState(int extraSpace) { return ViewInteraction.onCreateDrawableState(this, extraSpace); }
    protected static int[] mergeDrawableStates(int[] baseState, int[] additionalState) { return ViewInteraction.mergeDrawableStates(baseState, additionalState); }
    /** Recomputes the drawable state and pushes it to the drawables that depend on it. */
    public void refreshDrawableState() { ViewInteraction.refreshDrawableState(this); }
    protected void drawableStateChanged() { ViewInteraction.drawableStateChanged(this); }
    public void jumpDrawablesToCurrentState() { ViewInteraction.jumpDrawablesToCurrentState(this); }
    public void drawableHotspotChanged(float x, float y) { ViewInteraction.drawableHotspotChanged(this, x, y); }
    public void dispatchDrawableHotspotChanged(float x, float y) { }
    protected boolean verifyDrawable(android.graphics.drawable.Drawable who) { return who == mBackground || who == mForeground || who == mScrollIndicatorDrawable; }
    @Override
    public void invalidateDrawable(android.graphics.drawable.Drawable drawable) { ViewInteraction.invalidateDrawable(this, drawable); }
    @Override
    public void scheduleDrawable(android.graphics.drawable.Drawable who, Runnable what, long when) { ViewInteraction.scheduleDrawable(this, who, what, when); }
    @Override
    public void unscheduleDrawable(android.graphics.drawable.Drawable who, Runnable what) { ViewInteraction.unscheduleDrawable(this, who, what); }
    public void unscheduleDrawable(android.graphics.drawable.Drawable who) { }
    // Tints.
    android.content.res.ColorStateList mBackgroundTintList;
    android.graphics.PorterDuff.Mode mBackgroundTintMode;
    android.content.res.ColorStateList mForegroundTintList;
    android.graphics.PorterDuff.Mode mForegroundTintMode;
    int mForegroundGravity = Gravity.FILL;
    public android.content.res.ColorStateList getBackgroundTintList() { return mBackgroundTintList; }
    public void setBackgroundTintList(android.content.res.ColorStateList tint) { ViewInteraction.setBackgroundTintList(this, tint); }
    public android.graphics.PorterDuff.Mode getBackgroundTintMode() { return mBackgroundTintMode; }
    public void setBackgroundTintMode(android.graphics.PorterDuff.Mode tintMode) { ViewInteraction.setBackgroundTintMode(this, tintMode); }
    public void setBackgroundTintBlendMode(android.graphics.BlendMode blendMode) { setBackgroundTintMode(android.graphics.BlendMode.blendModeToPorterDuffMode(blendMode)); }
    private void applyBackgroundTint() { ViewInteraction.applyBackgroundTint(this); }
    public android.content.res.ColorStateList getForegroundTintList() { return mForegroundTintList; }
    public void setForegroundTintList(android.content.res.ColorStateList tint) { ViewInteraction.setForegroundTintList(this, tint); }
    public android.graphics.PorterDuff.Mode getForegroundTintMode() { return mForegroundTintMode; }
    public void setForegroundTintMode(android.graphics.PorterDuff.Mode tintMode) { ViewInteraction.setForegroundTintMode(this, tintMode); }
    public void setForegroundTintBlendMode(android.graphics.BlendMode blendMode) { setForegroundTintMode(android.graphics.BlendMode.blendModeToPorterDuffMode(blendMode)); }
    private void applyForegroundTint() { ViewInteraction.applyForegroundTint(this); }
    public int getForegroundGravity() { return mForegroundGravity; }
    public void setForegroundGravity(int gravity) { mForegroundGravity = gravity; }
    // ------------------------------------------------------------------------------------
    // Focus.
    int mNextFocusLeftId = NO_ID;
    int mNextFocusRightId = NO_ID;
    int mNextFocusUpId = NO_ID;
    int mNextFocusDownId = NO_ID;
    int mNextFocusForwardId = NO_ID;
    int mNextClusterForwardId = NO_ID;
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
    public OnFocusChangeListener getOnFocusChangeListener() { return mOnFocusChangeListener; }
    public int getFocusable() { return mFocusable ? FOCUSABLE : NOT_FOCUSABLE; }
    public boolean requestFocus(int direction) { return requestFocus(direction, null); }
    public boolean requestFocus(int direction, android.graphics.Rect previouslyFocusedRect) { return ViewInteraction.requestFocus(this, direction, previouslyFocusedRect); }
    public final boolean requestFocusFromTouch() { return requestFocus(FOCUS_DOWN); }
    public boolean restoreDefaultFocus() { return requestFocus(FOCUS_FORWARD); }
    public View findFocus() { return mHasFocus ? this : null; }
    public View focusSearch(int direction) { return mParent != null ? mParent.focusSearch(this, direction) : null; }
    public boolean hasFocusable() { return mFocusable && mVisibility == VISIBLE; }
    public boolean hasExplicitFocusable() { return hasFocusable(); }
    public java.util.ArrayList<View> getFocusables(int direction) { return ViewInteraction.getFocusables(this, direction); }
    public void addFocusables(java.util.ArrayList<View> views, int direction) { addFocusables(views, direction, FOCUSABLES_TOUCH_MODE); }
    public void addFocusables(java.util.ArrayList<View> views, int direction, int focusableMode) { ViewInteraction.addFocusables(this, views, direction, focusableMode); }
    public java.util.ArrayList<View> getTouchables() { return ViewInteraction.getTouchables(this); }
    public void addTouchables(java.util.ArrayList<View> views) { ViewInteraction.addTouchables(this, views); }
    public void findViewsWithText(java.util.ArrayList<View> outViews, CharSequence searched,
            int flags) { ViewInteraction.findViewsWithText(this, outViews, searched, flags); }
    public static final int FIND_VIEWS_WITH_TEXT = 0x00000001;
    public static final int FIND_VIEWS_WITH_CONTENT_DESCRIPTION = 0x00000002;
    public View keyboardNavigationClusterSearch(View currentCluster, int direction) { return null; }
    public void addKeyboardNavigationClusters(java.util.Collection<View> views, int direction) { }
    public boolean dispatchUnhandledMove(View focused, int direction) { return false; }
    public boolean dispatchKeyEventPreIme(KeyEvent event) { return onKeyPreIme(event.getKeyCode(), event); }
    public boolean onKeyPreIme(int keyCode, KeyEvent event) { return false; }
    public boolean dispatchKeyShortcutEvent(KeyEvent event) { return onKeyShortcut(event.getKeyCode(), event); }
    public boolean onKeyShortcut(int keyCode, KeyEvent event) { return false; }
    public boolean dispatchTrackballEvent(MotionEvent event) { return onTrackballEvent(event); }
    public boolean onTrackballEvent(MotionEvent event) { return false; }
    public boolean dispatchGenericMotionEvent(MotionEvent event) { return ViewInteraction.dispatchGenericMotionEvent(this, event); }
    public boolean onGenericMotionEvent(MotionEvent event) { return false; }
    protected boolean dispatchHoverEvent(MotionEvent event) { return ViewInteraction.dispatchHoverEvent(this, event); }
    public boolean onHoverEvent(MotionEvent event) { return false; }
    protected boolean dispatchGenericPointerEvent(MotionEvent event) { return false; }
    protected boolean dispatchGenericFocusedEvent(MotionEvent event) { return false; }
    public boolean dispatchCapturedPointerEvent(MotionEvent event) { return onCapturedPointerEvent(event); }
    public boolean onCapturedPointerEvent(MotionEvent event) { return false; }
    public void dispatchPointerCaptureChanged(boolean hasCapture) { onPointerCaptureChange(hasCapture); }
    public void onPointerCaptureChange(boolean hasCapture) { }
    public void requestPointerCapture() { }
    public void releasePointerCapture() { }
    public boolean hasPointerCapture() { return false; }
    public void requestUnbufferedDispatch(MotionEvent event) { }
    public boolean dispatchDragEvent(DragEvent event) { return ViewInteraction.dispatchDragEvent(this, event); }
    public boolean onDragEvent(DragEvent event) { return false; }
    public void cancelDragAndDrop() { }
    public void updateDragShadow(DragShadowBuilder shadowBuilder) { }
    public final boolean startDragAndDrop(android.content.ClipData data,
            DragShadowBuilder shadowBuilder, Object myLocalState, int flags) { return ViewInteraction.startDragAndDrop(this, data, shadowBuilder, myLocalState, flags); }
    public final boolean startDrag(android.content.ClipData data, DragShadowBuilder shadowBuilder,
            Object myLocalState, int flags) { return ViewInteraction.startDrag(this, data, shadowBuilder, myLocalState, flags); }
    public PointerIcon getPointerIcon() { return mPointerIcon; }
    public void setPointerIcon(PointerIcon pointerIcon) { mPointerIcon = pointerIcon; }
    public PointerIcon onResolvePointerIcon(MotionEvent event, int pointerIndex) { return null; }
    PointerIcon mPointerIcon;
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
    OnHoverListener mOnHoverListener;
    OnGenericMotionListener mOnGenericMotionListener;
    OnContextClickListener mOnContextClickListener;
    OnCapturedPointerListener mOnCapturedPointerListener;
    OnCreateContextMenuListener mOnCreateContextMenuListener;
    java.util.ArrayList<OnUnhandledKeyEventListener> mUnhandledKeyListeners;
    OnDragListener mOnDragListener;
    public void setOnHoverListener(OnHoverListener l) { mOnHoverListener = l; }
    public void setOnGenericMotionListener(OnGenericMotionListener l) { mOnGenericMotionListener = l; }
    public void setOnContextClickListener(OnContextClickListener l) { ViewInteraction.setOnContextClickListener(this, l); }
    public void setOnCapturedPointerListener(OnCapturedPointerListener l) { mOnCapturedPointerListener = l; }
    public void setOnCreateContextMenuListener(OnCreateContextMenuListener l) { mOnCreateContextMenuListener = l; }
    public void setOnDragListener(OnDragListener l) { mOnDragListener = l; }
    public void addOnUnhandledKeyEventListener(OnUnhandledKeyEventListener listener) { ViewInteraction.addOnUnhandledKeyEventListener(this, listener); }
    public void removeOnUnhandledKeyEventListener(OnUnhandledKeyEventListener listener) { if (mUnhandledKeyListeners != null) mUnhandledKeyListeners.remove(listener); }
    public boolean performContextClick(float x, float y) { return performContextClick(); }
    public boolean performContextClick() { return ViewInteraction.performContextClick(this); }
    public boolean performLongClick(float x, float y) { return performLongClick(); }
    public boolean showContextMenu() { return mParent != null && mParent.showContextMenuForChild(this); }
    public boolean showContextMenu(float x, float y) { return showContextMenu(); }
    protected ContextMenu.ContextMenuInfo getContextMenuInfo() { return null; }
    public void createContextMenu(ContextMenu menu) { ViewInteraction.createContextMenu(this, menu); }
    protected void onCreateContextMenu(ContextMenu menu) { }
    public ActionMode startActionMode(ActionMode.Callback callback) { return startActionMode(callback, ActionMode.TYPE_PRIMARY); }
    public ActionMode startActionMode(ActionMode.Callback callback, int type) { return mParent != null ? mParent.startActionModeForChild(this, callback, type) : null; }
    public boolean requestRectangleOnScreen(android.graphics.Rect rectangle) { return requestRectangleOnScreen(rectangle, false); }
    public boolean requestRectangleOnScreen(android.graphics.Rect rectangle, boolean immediate) { return ViewInteraction.requestRectangleOnScreen(this, rectangle, immediate); }
    // ------------------------------------------------------------------------------------
    // Nested scrolling (the child side of the protocol).
    boolean mNestedScrollingEnabled;
    ViewParent mNestedScrollingParent;
    public void setNestedScrollingEnabled(boolean enabled) { ViewInteraction.setNestedScrollingEnabled(this, enabled); }
    public boolean isNestedScrollingEnabled() { return mNestedScrollingEnabled; }
    public boolean startNestedScroll(int axes) { return ViewInteraction.startNestedScroll(this, axes); }
    public void stopNestedScroll() { ViewInteraction.stopNestedScroll(this); }
    public boolean hasNestedScrollingParent() { return mNestedScrollingParent != null; }
    public boolean dispatchNestedScroll(int dxConsumed, int dyConsumed, int dxUnconsumed,
            int dyUnconsumed, int[] offsetInWindow) { return ViewInteraction.dispatchNestedScroll(this, dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed, offsetInWindow); }
    public boolean dispatchNestedPreScroll(int dx, int dy, int[] consumed, int[] offsetInWindow) { return ViewInteraction.dispatchNestedPreScroll(this, dx, dy, consumed, offsetInWindow); }
    public boolean dispatchNestedFling(float velocityX, float velocityY, boolean consumed) { return ViewInteraction.dispatchNestedFling(this, velocityX, velocityY, consumed); }
    public boolean dispatchNestedPreFling(float velocityX, float velocityY) { return ViewInteraction.dispatchNestedPreFling(this, velocityX, velocityY); }
    public boolean dispatchNestedPrePerformAccessibilityAction(int action, Bundle arguments) { return ViewInteraction.dispatchNestedPrePerformAccessibilityAction(this, action, arguments); }
    // ------------------------------------------------------------------------------------
    // Accessibility, autofill and text/layout direction bookkeeping.
    int mImportantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_AUTO;
    int mImportantForAutofill = IMPORTANT_FOR_AUTOFILL_AUTO;
    int mAccessibilityLiveRegion = ACCESSIBILITY_LIVE_REGION_NONE;
    int mAccessibilityTraversalBefore = NO_ID;
    int mAccessibilityTraversalAfter = NO_ID;
    boolean mAccessibilityHeading;
    CharSequence mAccessibilityPaneTitle;
    int mTextDirection = TEXT_DIRECTION_INHERIT;
    int mTextAlignment = TEXT_ALIGNMENT_GRAVITY;
    int mLayoutDirection = LAYOUT_DIRECTION_INHERIT;
    int mResolvedLayoutDirection = -1;
    public int getImportantForAccessibility() { return mImportantForAccessibility; }
    public void setImportantForAccessibility(int mode) { mImportantForAccessibility = mode; }
    /** True when accessibility services should see this view, resolving the AUTO mode. */
    public boolean isImportantForAccessibility() { return ViewInteraction.isImportantForAccessibility(this); }
    public int getImportantForAutofill() { return mImportantForAutofill; }
    public void setImportantForAutofill(int mode) { mImportantForAutofill = mode; }
    public boolean isImportantForAutofill() { return ViewInteraction.isImportantForAutofill(this); }
    public void setAutofillHints(String... autofillHints) { }
    public AccessibilityDelegate getAccessibilityDelegate() { return mAccessibilityDelegate; }
    public CharSequence getAccessibilityClassName() { return View.class.getName(); }
    public int getAccessibilityLiveRegion() { return mAccessibilityLiveRegion; }
    public void setAccessibilityLiveRegion(int mode) { mAccessibilityLiveRegion = mode; }
    public boolean isAccessibilityHeading() { return mAccessibilityHeading; }
    public void setAccessibilityHeading(boolean isHeading) { mAccessibilityHeading = isHeading; }
    public CharSequence getAccessibilityPaneTitle() { return mAccessibilityPaneTitle; }
    public void setAccessibilityPaneTitle(CharSequence accessibilityPaneTitle) { mAccessibilityPaneTitle = accessibilityPaneTitle; }
    public int getAccessibilityTraversalBefore() { return mAccessibilityTraversalBefore; }
    public void setAccessibilityTraversalBefore(int beforeId) { mAccessibilityTraversalBefore = beforeId; }
    public int getAccessibilityTraversalAfter() { return mAccessibilityTraversalAfter; }
    public void setAccessibilityTraversalAfter(int afterId) { mAccessibilityTraversalAfter = afterId; }
    public boolean isAccessibilityFocused() { return false; }
    public ViewParent getParentForAccessibility() { return ViewInteraction.getParentForAccessibility(this); }
    public void sendAccessibilityEvent(int eventType) { ViewInteraction.sendAccessibilityEvent(this, eventType); }
    public void sendAccessibilityEventUnchecked(android.view.accessibility.AccessibilityEvent event) { }
    public void announceForAccessibility(CharSequence text) { }
    public boolean performAccessibilityAction(int action, Bundle arguments) { return ViewInteraction.performAccessibilityAction(this, action, arguments); }
    public boolean dispatchPopulateAccessibilityEvent(
            android.view.accessibility.AccessibilityEvent event) { return ViewInteraction.dispatchPopulateAccessibilityEvent(this, event); }
    public void onPopulateAccessibilityEvent(android.view.accessibility.AccessibilityEvent event) { }
    public void onInitializeAccessibilityEvent(android.view.accessibility.AccessibilityEvent event) { }
    public void onInitializeAccessibilityNodeInfo( android.view.accessibility.AccessibilityNodeInfo info) { }
    public android.view.accessibility.AccessibilityNodeInfo createAccessibilityNodeInfo() { return null; }
    public android.view.accessibility.AccessibilityNodeProvider getAccessibilityNodeProvider() { return ViewInteraction.getAccessibilityNodeProvider(this); }
    public void addChildrenForAccessibility(java.util.ArrayList<View> outChildren) { }
    public void notifySubtreeAccessibilityStateChanged(View child, View source, int changeType) { }
    public int getTextDirection() { return mTextDirection; }
    public void setTextDirection(int textDirection) { mTextDirection = textDirection; }
    public int getRawTextDirection() { return mTextDirection; }
    public boolean canResolveTextDirection() { return true; }
    public boolean isTextDirectionResolved() { return true; }
    public int getTextAlignment() { return mTextAlignment; }
    public void setTextAlignment(int textAlignment) { mTextAlignment = textAlignment; }
    public int getRawTextAlignment() { return mTextAlignment; }
    public boolean canResolveTextAlignment() { return true; }
    public boolean isTextAlignmentResolved() { return true; }
    public void setLayoutDirection(int layoutDirection) { ViewInteraction.setLayoutDirection(this, layoutDirection); }
    public int getRawLayoutDirection() { return mLayoutDirection; }
    public boolean canResolveLayoutDirection() { return true; }
    public boolean isLayoutDirectionResolved() { return true; }
    public void setAnimation(android.view.animation.Animation animation) { ViewInteraction.setAnimation(this, animation); }
    // ------------------------------------------------------------------------------------
    // Legacy view animation. The animation is stepped once per frame so its listeners and
    // end state behave; the transformation is not applied to drawing.
    android.view.animation.Animation mCurrentAnimation;
    Choreographer.FrameCallback mAnimationTicker;
    public android.view.animation.Animation getAnimation() { return mCurrentAnimation; }
    public void startAnimation(android.view.animation.Animation animation) { ViewInteraction.startAnimation(this, animation); }
    public void clearAnimation() { ViewInteraction.clearAnimation(this); }
    protected void onAnimationStart() { }
    protected void onAnimationEnd() { }
    protected boolean onSetAlpha(int alpha) { return false; }
    private void scheduleAnimationTick() { ViewInteraction.scheduleAnimationTick(this); }
    // ------------------------------------------------------------------------------------
    // Instance state.
    protected android.os.Parcelable onSaveInstanceState() { return BaseSavedState.EMPTY_STATE; }
    protected void onRestoreInstanceState(android.os.Parcelable state) { ViewInteraction.onRestoreInstanceState(this, state); }
    public void saveHierarchyState(android.util.SparseArray<android.os.Parcelable> container) { dispatchSaveInstanceState(container); }
    protected void dispatchSaveInstanceState(android.util.SparseArray<android.os.Parcelable> container) { ViewInteraction.dispatchSaveInstanceState(this, container); }
    public void restoreHierarchyState(android.util.SparseArray<android.os.Parcelable> container) { dispatchRestoreInstanceState(container); }
    protected void dispatchRestoreInstanceState(
            android.util.SparseArray<android.os.Parcelable> container) { ViewInteraction.dispatchRestoreInstanceState(this, container); }
    void sendAccessibilityEventInternal(int eventType) { }
    static void moduleSetKeepScreenOnNative(boolean keepOn) { setKeepScreenOnNative(keepOn); }
    static android.os.Handler moduleGetRunQueue(View hostView) { return hostView.getRunQueue(); }
}
