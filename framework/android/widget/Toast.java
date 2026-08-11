package android.widget;

import android.content.Context;

/**
 * triển khai android.widget.toast tối thiểu.
 *
 * hiển thị một thông báo ngắn gọn. đối với khuôn khổ tối thiểu của kudroid, điều này ghi nhật ký
 * thông báo và không hiển thị ui.
 */
public class Toast {
    /** hiển thị trong thời gian ngắn. */
    public static final int LENGTH_SHORT = 0;
    /** hiển thị trong thời gian dài. */
    public static final int LENGTH_LONG = 1;

    private final Context mContext;
    private CharSequence mText;
    private int mDuration;

    private Toast(Context context) {
        mContext = context;
    }

    /**
     * tạo một toast.
     */
    public static Toast makeText(Context context, CharSequence text, int duration) {
        Toast toast = new Toast(context);
        toast.mText = text;
        toast.mDuration = duration;
        return toast;
    }

    /**
     * tạo một toast từ một id tài nguyên.
     */
    public static Toast makeText(Context context, int resId, int duration) {
        return makeText(context, "", duration);
    }

    /**
     * hiển thị toast.
     */
    public void show() {
        // Log the message; no UI for now.
        android.util.Log.i("Toast", mText != null ? mText.toString() : "");
    }

    /**
     * thiết lập văn bản.
     */
    public void setText(CharSequence s) {
        mText = s;
    }

    /**
     * thiết lập khoảng thời gian.
     */
    public void setDuration(int duration) {
        mDuration = duration;
    }
}
