package android.view;

import android.content.Context;

/**
 * Minimal android.view.Window implementation.
 *
 * Represents a window. For KuDroid's minimal framework, this is a stub that
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
     * Return the context this window was created with.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * Set the content view.
     */
    public void setContentView(int layoutResID) {
    }

    /**
     * Set the content view to a view.
     */
    public void setContentView(View view) {
        mDecorView = view;
    }

    /**
     * Return the decor view.
     */
    public View getDecorView() {
        return mDecorView;
    }

    /**
     * Set a window flag.
     */
    public void setFlags(int flags, int mask) {
        mFlags = (mFlags & ~mask) | (flags & mask);
    }

    /**
     * Add a window flag.
     */
    public void addFlags(int flags) {
        mFlags |= flags;
    }

    /**
     * Clear a window flag.
     */
    public void clearFlags(int flags) {
        mFlags &= ~flags;
    }

    /**
     * Return the current window flags.
     */
    public int getFlags() {
        return mFlags;
    }

    /**
     * Set the window background.
     */
    public void setBackgroundDrawable(android.graphics.drawable.Drawable drawable) {
    }

    /**
     * Set the window title.
     */
    public void setTitle(CharSequence title) {
    }
}
