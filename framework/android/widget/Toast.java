package android.widget;

import android.content.Context;

/**
 * minimal android.widget.toast implementation.
 *
 * displays a brief message. for kudroid minimal framework this does logging
 *notification and no ui display.
 */
public class Toast {
    /** displays briefly. */
    public static final int LENGTH_SHORT = 0;
    /** displayed for a long time. */
    public static final int LENGTH_LONG = 1;

    private final Context mContext;
    private CharSequence mText;
    private int mDuration;

    private Toast(Context context) {
        mContext = context;
    }

    /**
     * create a toast.
     */
    public static Toast makeText(Context context, CharSequence text, int duration) {
        Toast toast = new Toast(context);
        toast.mText = text;
        toast.mDuration = duration;
        return toast;
    }

    /**
     * create a toast from a resource id.
     */
    public static Toast makeText(Context context, int resId, int duration) {
        return makeText(context, "", duration);
    }

    /**
     * show toast.
     */
    public void show() {
        // Log the message; no UI for now.
        android.util.Log.i("Toast", mText != null ? mText.toString() : "");
    }

    /**
     * set text.
     */
    public void setText(CharSequence s) {
        mText = s;
    }

    /**
     * set time period.
     */
    public void setDuration(int duration) {
        mDuration = duration;
    }
}
