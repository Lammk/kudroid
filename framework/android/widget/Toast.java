package android.widget;

import android.content.Context;
import android.view.View;
import android.util.Log;

public class Toast {
    public static final int LENGTH_SHORT = 0;
    public static final int LENGTH_LONG = 1;

    private final Context mContext;
    private int mDuration = LENGTH_SHORT;
    private CharSequence mText;

    public Toast(Context context) { mContext = context; }
    public void show() {
        Log.i("Toast", "[TOAST] " + (mText != null ? mText.toString() : ""));
    }
    public void cancel() {}
    public void setView(View view) {}
    public View getView() { return null; }
    public void setDuration(int duration) { mDuration = duration; }
    public int getDuration() { return mDuration; }
    public void setGravity(int gravity, int xOffset, int yOffset) {}
    public int getGravity() { return 0; }
    public void setText(int resId) { if (mContext != null) mText = mContext.getText(resId); }
    public void setText(CharSequence s) { mText = s; }

    public static Toast makeText(Context context, CharSequence text, int duration) {
        Toast result = new Toast(context);
        result.mText = text;
        result.mDuration = duration;
        return result;
    }
    public static Toast makeText(Context context, int resId, int duration) {
        return makeText(context, context != null ? context.getResources().getText(resId) : "", duration);
    }
}
