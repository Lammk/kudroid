package android.widget;

import android.content.Context;

/**
 * Minimal android.widget.Toast implementation.
 *
 * Shows a brief message. For KuDroid's minimal framework, this logs the
 * message and does not display a UI.
 */
public class Toast {
    /** Show for a short duration. */
    public static final int LENGTH_SHORT = 0;
    /** Show for a long duration. */
    public static final int LENGTH_LONG = 1;

    private final Context mContext;
    private CharSequence mText;
    private int mDuration;

    private Toast(Context context) {
        mContext = context;
    }

    /**
     * Make a toast.
     */
    public static Toast makeText(Context context, CharSequence text, int duration) {
        Toast toast = new Toast(context);
        toast.mText = text;
        toast.mDuration = duration;
        return toast;
    }

    /**
     * Make a toast from a resource id.
     */
    public static Toast makeText(Context context, int resId, int duration) {
        return makeText(context, "", duration);
    }

    /**
     * Show the toast.
     */
    public void show() {
        // Log the message; no UI for now.
        android.util.Log.i("Toast", mText != null ? mText.toString() : "");
    }

    /**
     * Set the text.
     */
    public void setText(CharSequence s) {
        mText = s;
    }

    /**
     * Set the duration.
     */
    public void setDuration(int duration) {
        mDuration = duration;
    }
}
