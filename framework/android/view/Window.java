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

    private ViewGroup mDecorGroup;

    private ViewGroup decorGroup() {
        if (mDecorGroup == null) {
            android.widget.FrameLayout decor = new android.widget.FrameLayout(mContext);
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

    public interface Callback {
    }

    public interface OnFrameMetricsAvailableListener {
    }

}
