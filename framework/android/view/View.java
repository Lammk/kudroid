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
public class View {
    /** view visibility: visible. */
    public static final int VISIBLE = 0;
    /** view visibility: invisible. */
    public static final int INVISIBLE = 4;
    /** view visibility: disappears. */
    public static final int GONE = 8;

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
    private int mMeasuredWidth;
    private int mMeasuredHeight;
    protected int mScrollX = 0;
    protected int mScrollY = 0;

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
        Object onApplyWindowInsets(View v, Object insets);
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
        mContext = context;
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
        if (mId == id) return this;
        return null;
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
        mLeft = l;
        mTop = t;
        mRight = r;
        mBottom = b;
        mLaidOut = true;
        onLayout(changed, l, t, r, b);
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
     * returns the view's parent.
     */
    public ViewGroup getParent() {
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
        mOnClickListener = l;
    }

    public void setOnLongClickListener(OnLongClickListener l) {
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
        return mOnClickListener != null;
    }

    public boolean isLongClickable() {
        return mOnLongClickListener != null;
    }

    public boolean hasFocus() {
        return mHasFocus;
    }

    public boolean isFocused() {
        return mHasFocus;
    }

    /** Change focus state and fire callback if there is a real change. */
    public void setFocus(boolean hasFocus) {
        if (mHasFocus == hasFocus) return;
        mHasFocus = hasFocus;
        if (mOnFocusChangeListener != null) {
            mOnFocusChangeListener.onFocusChange(this, hasFocus);
        }
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event == null) return false;
        if (mOnKeyListener != null &&
            mOnKeyListener.onKey(this, event.getKeyCode(), event)) {
            return true;
        }
        return onKeyDown(event.getKeyCode(), event);
    }

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return false;
    }

    // ── system UI visibility ────────────────────────────────────────────────
    //
    // Deprecated in AOSP since API 30 but still what androidx.core's compat layer
    // reads on older API levels, and it reaches these through the decor view:
    //
    //   WindowCompat.setDecorFitsSystemWindows(window, false)
    //       -> decorView.getSystemUiVisibility() | SYSTEM_UI_FLAG_LAYOUT_STABLE ...
    //       -> decorView.setSystemUiVisibility(newFlags)
    //
    // That call sits in GameActivity.createSurfaceView, so its absence stopped
    // Minecraft's onCreate outright. Auto-stubbing the getter would have been worse
    // than useless here: it returns 0 and the setter discards, so the flags round-trip
    // as zero and any app that reads back what it set sees the wrong value.

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

    // ── layout params ───────────────────────────────────────────────────────
    //
    // A view added to a container carries its own sizing request. Returning null from
    // getLayoutParams — which had no storage at all before — makes the standard
    // read-modify-write idiom throw:
    //
    //   ViewGroup.LayoutParams lp = view.getLayoutParams();
    //   lp.height = ...;                 // NPE
    //
    private ViewGroup.LayoutParams mLayoutParams;

    public ViewGroup.LayoutParams getLayoutParams() {
        return mLayoutParams;
    }

    public void setLayoutParams(ViewGroup.LayoutParams params) {
        mLayoutParams = params;
        requestLayout();
    }

    // ── IME attachment ──────────────────────────────────────────────────────

    /**
     * Create the InputConnection through which an IME edits this view's text.
     *
     * Returning null means "this view does not accept text", which is right for a
     * plain View. A view that does — an EditText, or a game's own surface — overrides
     * this and hands back its own connection; Minecraft's GameActivity does exactly
     * that, which is why BaseInputConnection has to be usable as a superclass.
     */
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
        return true;
    }

    public void setFocusable(boolean focusable) {
    }

    public void setFocusableInTouchMode(boolean focusable) {
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
        if (mParent != null) return mParent.getViewTreeObserver();
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
            return mOnClickListener != null || mOnLongClickListener != null;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            if (!mLongClickFired && mOnLongClickListener != null && mTouchDownTime > 0 &&
                android.os.SystemClock.uptimeMillis() - mTouchDownTime >= LONG_PRESS_TIMEOUT) {
                mLongClickFired = true;
                return performLongClick();
            }
            return mOnClickListener != null || mOnLongClickListener != null;
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
            return mOnLongClickListener != null;
        }
        return mOnClickListener != null || mOnLongClickListener != null;
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

    /**
     * returns the background of the view.
     */
    public android.graphics.drawable.Drawable getBackground() {
        return null;
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
    public void setBackgroundDrawable(android.graphics.drawable.Drawable background) {
    }

    /**
     * sets the view's padding.
     */
    public void setPadding(int left, int top, int right, int bottom) {
    }

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

    /**
     * post a runnable to the ui stream.
     */
    public boolean post(Runnable action) {
        action.run();
        return true;
    }

    // ── tags ────────────────────────────────────────────────────────────────
    //
    // A tag is arbitrary data an app attaches to a view, either unkeyed or under an
    // integer key. Libraries use the keyed form heavily to stash per-view state
    // without subclassing — androidx.core stores window-insets and lifecycle
    // bookkeeping this way.
    //
    // getTag() previously returned a hard null and there was no setter at all, so
    // storing then reading back gave null: an app that keeps its own state in a tag
    // silently loses it, and a library that caches a helper per view rebuilds it on
    // every call or dereferences the null it did not expect.
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

    // ── window insets ───────────────────────────────────────────────────────

    private OnApplyWindowInsetsListener mOnApplyWindowInsetsListener;

    /**
     * Install a listener that gets to consume window insets before this view does.
     *
     * androidx.core.view.ViewCompat sets one whenever an app opts out of
     * fits-system-windows, which is the modern way to draw behind the status bar —
     * GameActivity does it while building its surface. Auto-stubbing the setter meant
     * the listener was dropped, so an app relying on it to inset its own content laid
     * out under the system bars instead.
     */
    public void setOnApplyWindowInsetsListener(OnApplyWindowInsetsListener listener) {
        mOnApplyWindowInsetsListener = listener;
    }

    public OnApplyWindowInsetsListener getOnApplyWindowInsetsListener() {
        return mOnApplyWindowInsetsListener;
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

    public interface OnLayoutChangeListener {
    }

    public interface OnSystemUiVisibilityChangeListener {
        void onSystemUiVisibilityChange(int visibility);
    }

    public interface OnUnhandledKeyEventListener {
    }

}
