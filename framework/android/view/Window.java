package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.widget.FrameLayout;

import java.util.ArrayList;

/**
 * minimal android.view.window implementation.
 *
 * represents a window. for kudroid minimal framework, here is an emulation
 * provides basic window management.
 */
public class Window {
    public static final int FEATURE_OPTIONS_PANEL = 0;
    public static final int FEATURE_NO_TITLE = 1;
    public static final int FEATURE_PROGRESS = 2;
    public static final int FEATURE_LEFT_ICON = 3;
    public static final int FEATURE_RIGHT_ICON = 4;
    public static final int FEATURE_INDETERMINATE_PROGRESS = 5;
    public static final int FEATURE_CONTEXT_MENU = 6;
    public static final int FEATURE_CUSTOM_TITLE = 7;
    public static final int FEATURE_ACTION_BAR = 8;
    public static final int FEATURE_ACTION_BAR_OVERLAY = 9;
    public static final int FEATURE_ACTION_MODE_OVERLAY = 10;
    public static final int FEATURE_SWIPE_TO_DISMISS = 11;
    public static final int FEATURE_CONTENT_TRANSITIONS = 12;
    public static final int FEATURE_ACTIVITY_TRANSITIONS = 13;

    private final Context mContext;
    private DecorView mDecorView;
    private FrameLayout mContentParent;
    private View mContentView;
    private int mFlags;
    private volatile int mWidth;
    private volatile int mHeight;
    private volatile boolean mSurfaceReady;
    private volatile boolean mSurfaceCreated;
    private final Object mSurfaceLock = new Object();
    private final ArrayList<SurfaceCallbackState> mSurfaceCallbacks =
            new ArrayList<SurfaceCallbackState>();
    private SurfaceHolder.Callback mTakeSurfaceCallback;
    private SurfaceHolder.Callback mActivitySurfaceCallback;
    private final Surface mSurface = new Surface();
    private SurfaceHolder mSurfaceHolder;

    /** The root of a Window's view hierarchy. */
    public static class DecorView extends FrameLayout {
        public DecorView(Context context) {
            super(context);
        }
    }

    private static final class SurfaceCallbackState {
        final SurfaceHolder.Callback callback;
        boolean created;
        int width;
        int height;

        SurfaceCallbackState(SurfaceHolder.Callback callback) {
            this.callback = callback;
        }
    }

    public Window(Context context) {
        mContext = context;
    }

    public void takeSurface(android.view.SurfaceHolder.Callback2 callback) {
        if (mTakeSurfaceCallback == callback) return;
        if (mTakeSurfaceCallback != null) removeSurfaceCallback(mTakeSurfaceCallback);
        mTakeSurfaceCallback = callback;
        if (callback != null) addSurfaceCallback(callback);
    }

    /** Register the Activity's Window-surface callbacks when it implements them. */
    public void setActivitySurfaceCallback(SurfaceHolder.Callback callback) {
        if (mActivitySurfaceCallback == callback) return;
        if (mActivitySurfaceCallback != null) removeSurfaceCallback(mActivitySurfaceCallback);
        mActivitySurfaceCallback = callback;
        if (callback != null) addSurfaceCallback(callback);
    }

    public void takeInputQueue(android.view.InputQueue.Callback callback) {}

    /**
     * returns the context this window was created with.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * set content view.
     */
    public void setContentView(int layoutResID) {
    }

    /**
     * sets the content view to a view.
     */
    public void setContentView(View view) {
        mContentView = view;
        final FrameLayout content = contentParent();
        content.removeAllViews();
        if (view != null) {
            ViewParent parent = view.getParent();
            if (parent instanceof ViewGroup && parent != content) {
                ((ViewGroup) parent).removeView(view);
            }
            if (view.getLayoutParams() == null) {
                view.setLayoutParams(new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT));
            }
            content.addView(view);
        }
    }

    public View getContentView() {
        return mContentView;
    }

    /**
     * The root view of the window.
     *
     * Never null. Returning null — which happened whenever setContentView had not run
     * yet — breaks the standard idiom, which is to chain straight off it without a
     * check:
     *
     *   WindowCompat.setDecorFitsSystemWindows(window, false)
     *       -> window.getDecorView().getSystemUiVisibility()
     *
     * That is androidx code, it runs during onCreate on essentially every modern app,
     * and it is where Minecraft's launch failed with a NullPointerException. Creating
     * the decor view on demand is also what AOSP does — installDecor() runs before any
     * caller can observe a null.
     */
    public View getDecorView() {
        return ensureDecorView();
    }

    public View findViewById(int id) {
        if (id == android.R.id.content || id == 0x01020002) return contentParent();
        return ensureDecorView().findViewById(id);
    }

    private DecorView ensureDecorView() {
        if (mDecorView == null) {
            mDecorView = new DecorView(mContext);
            mDecorView.setLayoutParams(new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            mContentParent = new FrameLayout(mContext);
            mContentParent.setId(android.R.id.content);
            mContentParent.setLayoutParams(new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            mDecorView.addView(mContentParent);
        }
        return mDecorView;
    }

    private FrameLayout contentParent() {
        ensureDecorView();
        return mContentParent;
    }

    /** Update the Window bounds from the host's live Metal framebuffer. */
    public void updateSurfaceSize(int width, int height, boolean surfaceReady) {
        final boolean ready = surfaceReady && width > 0 && height > 0;
        mSurfaceReady = ready;
        mWidth = ready ? width : 0;
        mHeight = ready ? height : 0;
        if (!ready && mSurfaceCreated) dispatchSurfaceDestroyed();
    }

    public int getWidth() {
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }

    public boolean hasSurface() {
        return mSurfaceReady && mWidth > 0 && mHeight > 0;
    }

    /** Measure and lay out the full decor hierarchy at the actual surface size. */
    public boolean measureAndLayout() {
        if (!hasSurface()) return false;
        DecorView decor = ensureDecorView();
        final int widthSpec = View.MeasureSpec.makeMeasureSpec(mWidth, View.MeasureSpec.EXACTLY);
        final int heightSpec = View.MeasureSpec.makeMeasureSpec(mHeight, View.MeasureSpec.EXACTLY);
        decor.measure(widthSpec, heightSpec);
        decor.layout(0, 0, mWidth, mHeight);
        return true;
    }

    /** Deliver the current real Window surface state to Window callbacks. */
    public void dispatchSurfaceReady() {
        if (!hasSurface()) return;
        mSurface.setSurfaceSize(mWidth, mHeight);
        SurfaceCallbackState[] callbacks;
        synchronized (mSurfaceLock) {
            mSurfaceCreated = true;
            callbacks = mSurfaceCallbacks.toArray(new SurfaceCallbackState[mSurfaceCallbacks.size()]);
        }
        for (SurfaceCallbackState state : callbacks) dispatchSurfaceState(state);
    }

    /** Deliver Window-surface destruction before the Activity is destroyed. */
    public void dispatchSurfaceDestroyed() {
        SurfaceCallbackState[] callbacks;
        synchronized (mSurfaceLock) {
            if (!mSurfaceCreated) {
                mSurface.clearSurface();
                return;
            }
            mSurfaceCreated = false;
            callbacks = mSurfaceCallbacks.toArray(new SurfaceCallbackState[mSurfaceCallbacks.size()]);
        }
        mSurface.clearSurface();
        for (SurfaceCallbackState state : callbacks) {
            boolean notify;
            synchronized (mSurfaceLock) {
                notify = state.created && mSurfaceCallbacks.contains(state);
                state.created = false;
                state.width = 0;
                state.height = 0;
            }
            if (notify) invokeSurfaceDestroyed(state.callback);
        }
    }

    public SurfaceHolder getSurfaceHolder() {
        if (mSurfaceHolder == null) mSurfaceHolder = new WindowSurfaceHolder();
        return mSurfaceHolder;
    }

    private void addSurfaceCallback(SurfaceHolder.Callback callback) {
        if (callback == null) return;
        SurfaceCallbackState state = null;
        synchronized (mSurfaceLock) {
            for (int i = 0; i < mSurfaceCallbacks.size(); i++) {
                if (mSurfaceCallbacks.get(i).callback == callback) return;
            }
            state = new SurfaceCallbackState(callback);
            mSurfaceCallbacks.add(state);
        }
        if (mSurfaceCreated) {
            final SurfaceCallbackState pending = state;
            ensureDecorView().post(new Runnable() {
                @Override
                public void run() {
                    dispatchSurfaceState(pending);
                }
            });
        }
    }

    private void removeSurfaceCallback(SurfaceHolder.Callback callback) {
        synchronized (mSurfaceLock) {
            for (int i = mSurfaceCallbacks.size() - 1; i >= 0; i--) {
                if (mSurfaceCallbacks.get(i).callback == callback) mSurfaceCallbacks.remove(i);
            }
        }
    }

    private void dispatchSurfaceState(SurfaceCallbackState state) {
        boolean created;
        boolean changed;
        synchronized (mSurfaceLock) {
            if (!mSurfaceCreated || !mSurfaceCallbacks.contains(state)) return;
            created = !state.created;
            changed = created || state.width != mWidth || state.height != mHeight;
            state.created = true;
            state.width = mWidth;
            state.height = mHeight;
        }
        if (created) invokeSurfaceCreated(state.callback);
        if (changed) {
            invokeSurfaceChanged(state.callback, mWidth, mHeight);
            if (state.callback instanceof SurfaceHolder.Callback2) {
                invokeSurfaceRedrawNeeded((SurfaceHolder.Callback2) state.callback);
            }
        }
    }

    private void invokeSurfaceCreated(SurfaceHolder.Callback callback) {
        try { callback.surfaceCreated(getSurfaceHolder()); }
        catch (Throwable t) { android.util.Log.e("Window", "surfaceCreated failed: " + t); }
    }

    private void invokeSurfaceChanged(SurfaceHolder.Callback callback, int width, int height) {
        try { callback.surfaceChanged(getSurfaceHolder(), 0, width, height); }
        catch (Throwable t) { android.util.Log.e("Window", "surfaceChanged failed: " + t); }
    }

    private void invokeSurfaceRedrawNeeded(SurfaceHolder.Callback2 callback) {
        try { callback.surfaceRedrawNeeded(getSurfaceHolder()); }
        catch (Throwable t) { android.util.Log.e("Window", "surfaceRedrawNeeded failed: " + t); }
    }

    private void invokeSurfaceDestroyed(SurfaceHolder.Callback callback) {
        try { callback.surfaceDestroyed(getSurfaceHolder()); }
        catch (Throwable t) { android.util.Log.e("Window", "surfaceDestroyed failed: " + t); }
    }

    private final class WindowSurfaceHolder implements SurfaceHolder {
        @Override public void addCallback(SurfaceHolder.Callback callback) { addSurfaceCallback(callback); }
        @Override public void removeCallback(SurfaceHolder.Callback callback) { removeSurfaceCallback(callback); }
        @Override public Surface getSurface() { return mSurface; }
        @Override public Rect getSurfaceFrame() { return new Rect(0, 0, mWidth, mHeight); }
        @Override public boolean isCreating() { return false; }
        @Override public void setType(int type) {}
        @Override public void setFixedSize(int width, int height) {}
        @Override public void setSizeFromLayout() {}
        @Override public void setFormat(int format) {}
        @Override public void setKeepScreenOn(boolean screenOn) {}
        @Override public Canvas lockCanvas() { return mSurface.lockCanvas(); }
        @Override public Canvas lockCanvas(Rect dirty) { return mSurface.lockCanvas(dirty); }
        @Override public void unlockCanvasAndPost(Canvas canvas) { mSurface.unlockCanvasAndPost(canvas); }
        @Override public Canvas lockCanvasAndroidOnly(Rect dirty) { return mSurface.lockCanvas(dirty); }
    }

    private static native void setKeepScreenOnNative(boolean keepOn);

    /**
     * set window flags.
     */
    public void setFlags(int flags, int mask) {
        mFlags = (mFlags & ~mask) | (flags & mask);
        if ((mask & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0) {
            try {
                setKeepScreenOnNative((mFlags & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0);
            } catch (Throwable ignored) {}
        }
    }

    /**
     * added a window flag.
     */
    public void addFlags(int flags) {
        mFlags |= flags;
        if ((flags & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0) {
            try {
                setKeepScreenOnNative(true);
            } catch (Throwable ignored) {}
        }
    }

    /**
     * remove a window flag.
     */
    public void clearFlags(int flags) {
        mFlags &= ~flags;
        if ((flags & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0) {
            try {
                setKeepScreenOnNative(false);
            } catch (Throwable ignored) {}
        }
    }

    /**
* tr  v  c c c  c a s  hi n t i.
     */
    public int getFlags() {
        return mFlags;
    }

    /**
     * set window background.
     */
    public void setBackgroundDrawable(android.graphics.drawable.Drawable drawable) {
    }

    /**
     * set window title.
     */
    public void setTitle(CharSequence title) {
    }

    /**
     * The pixel format the window's surface should use.
     *
     * A no-op on KuDroid: the surface is a CAMetalLayer whose format is fixed at
     * bgra8Unorm by the host, and there is no path to renegotiate it. Recorded rather
     * than discarded so getFormat() reports back what the app asked for — an app that
     * sets RGBA_8888 and reads it back to confirm should not see 0.
     */
    private int mFormat = android.graphics.PixelFormat.OPAQUE;

    public void setFormat(int format) {
        mFormat = format;
    }

    public int getFormat() {
        return mFormat;
    }

    /**
     * How the window reacts to the soft keyboard appearing.
     *
     * Stored, not acted on. Resizing or panning the window for the keyboard is the
     * host's business — iOS reports keyboard geometry through its own notifications —
     * but the value has to survive a round trip because apps read it back to decide
     * whether they already configured the window.
     */
    private int mSoftInputMode =
            WindowManager.LayoutParams.SOFT_INPUT_STATE_UNSPECIFIED;

    public void setSoftInputMode(int mode) {
        mSoftInputMode = mode;
    }

    public int getSoftInputMode() {
        return mSoftInputMode;
    }

    /**
     * Window features, requested before the content view is set.
     *
     * Recorded and reported through hasFeature() rather than ignored: apps ask for
     * FEATURE_NO_TITLE and then check, and a window that forgets what was requested makes
     * that check disagree with what was asked for. Nothing here draws a title bar, so
     * granting every request is honest — the feature's effect is already the default.
     */
    private int mFeatures;

    public boolean requestFeature(int featureId) {
        mFeatures |= (1 << featureId);
        return true;
    }

    public boolean hasFeature(int featureId) {
        return (mFeatures & (1 << featureId)) != 0;
    }

    /**
     * The layout parameters of this window.
     *
     * One instance, kept: apps read them, mutate a field, and call
     * WindowManager.updateViewLayout with the same object. Handing back a fresh copy would
     * accept those mutations and discard them.
     */
    private WindowManager.LayoutParams mAttributes;

    public WindowManager.LayoutParams getAttributes() {
        if (mAttributes == null) {
            mAttributes = new WindowManager.LayoutParams();
        }
        return mAttributes;
    }

    public void setAttributes(WindowManager.LayoutParams params) {
        mAttributes = params;
    }

    /** Size the window; KuDroid runs everything full-screen, so this only records intent. */
    public void setLayout(int width, int height) {
        final WindowManager.LayoutParams params = getAttributes();
        params.width = width;
        params.height = height;
    }

    /**
     * System-bar colours and inset behaviour.
     *
     * No-ops: the guest draws into a Metal layer that occupies the whole screen, and iOS
     * owns the status bar. Present because apps set them during theme setup and a missing
     * method there stops the Activity before it draws anything.
     */
    public void setStatusBarColor(int color) {
    }

    public void setNavigationBarColor(int color) {
    }

    public void setStatusBarContrastEnforced(boolean enforced) {
    }

    public void setNavigationBarContrastEnforced(boolean enforced) {
    }

    public void setDecorFitsSystemWindows(boolean decorFitsSystemWindows) {
    }

    private Callback mCallback;

    public void setCallback(Callback callback) {
        mCallback = callback;
    }

    public Callback getCallback() {
        return mCallback;
    }

    /**
     * The decor view if one exists, WITHOUT creating it.
     *
     * The distinction from getDecorView() is the whole point: callers use peekDecorView to
     * test whether a window has been laid out yet, and a version that creates on demand
     * always answers yes.
     */
    public View peekDecorView() {
        return mDecorView;
    }

    public interface Callback {
        public boolean dispatchKeyEvent(KeyEvent event);
        public boolean dispatchKeyShortcutEvent(KeyEvent event);
        public boolean dispatchTouchEvent(MotionEvent event);
        public boolean dispatchTrackballEvent(MotionEvent event);
        public boolean dispatchGenericMotionEvent(MotionEvent event);
        public boolean dispatchPopulateAccessibilityEvent(android.view.accessibility.AccessibilityEvent event);
        public View onCreatePanelView(int featureId);
        public boolean onCreatePanelMenu(int featureId, Menu menu);
        public boolean onPreparePanel(int featureId, View view, Menu menu);
        public boolean onMenuOpened(int featureId, Menu menu);
        public boolean onMenuItemSelected(int featureId, MenuItem item);
        public void onWindowAttributesChanged(WindowManager.LayoutParams attrs);
        public void onContentChanged();
        public void onWindowFocusChanged(boolean hasFocus);
        public void onAttachedToWindow();
        public void onDetachedFromWindow();
        public void onPanelClosed(int featureId, Menu menu);
        public boolean onSearchRequested();
        public boolean onSearchRequested(SearchEvent searchEvent);
        public ActionMode onWindowStartingActionMode(ActionMode.Callback callback);
        public ActionMode onWindowStartingActionMode(ActionMode.Callback callback, int type);
        public void onActionModeStarted(ActionMode mode);
        public void onActionModeFinished(ActionMode mode);
        default public void onPointerCaptureChanged(boolean hasCapture) {}
    }

    public interface OnFrameMetricsAvailableListener {
    }

    public boolean superDispatchKeyEvent(KeyEvent event) {
        ViewGroup decor = ensureDecorView();
        if (decor != null) {
            return decor.dispatchKeyEvent(event);
        }
        return false;
    }

    public boolean superDispatchTouchEvent(MotionEvent event) {
        ViewGroup decor = ensureDecorView();
        if (decor != null) {
            return decor.dispatchTouchEvent(event);
        }
        return false;
    }
}
