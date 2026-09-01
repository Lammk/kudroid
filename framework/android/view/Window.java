package android.view;

import android.content.Context;

/**
 * minimal android.view.window implementation.
 *
 * represents a window. for kudroid minimal framework, here is an emulation
 * provides basic window management.
 */
public class Window {
    private final Context mContext;
    private View mDecorView;
    private View mContentView;
    private int mFlags;

    public Window(Context context) {
        mContext = context;
    }

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
        // Android parents the content view under the decor view rather than replacing
        // it. Keeping them separate matters because getDecorView() is what window
        // insets, system-UI visibility and IME attachment are all read from.
        if (view != null) {
            final ViewGroup decor = decorGroup();
            if (decor != null && view.getParent() != decor) decor.addView(view);
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
        return decorGroup();
    }

    public View findViewById(int id) {
        ViewGroup decor = decorGroup();
        if (decor != null) {
            if (decor.getId() == id) return decor;
            View v = decor.findViewById(id);
            if (v != null) return v;
            if (id == android.R.id.content || id == 0x01020002) {
                return decor;
            }
        }
        return null;
    }

    private ViewGroup mDecorGroup;

    private ViewGroup decorGroup() {
        if (mDecorGroup == null) {
            android.widget.FrameLayout decor = new android.widget.FrameLayout(mContext);
            decor.setId(android.R.id.content);
            decor.setLayoutParams(new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
            mDecorGroup = decor;
            mDecorView = decor;
        }
        return mDecorGroup;
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
        return getContentView();
    }

    public interface Callback {
    }

    public interface OnFrameMetricsAvailableListener {
    }

}
