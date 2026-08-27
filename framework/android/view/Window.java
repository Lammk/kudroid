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
        mDecorView = view;
    }

    /**
     * returns decoration view.
     */
    public View getDecorView() {
        return mDecorView;
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
