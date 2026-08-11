package android.app;

import android.content.Context;
import android.content.DialogInterface;

/**
 * triển khai android.app.dialog tối thiểu.
 *
 * lớp cơ sở cho các hộp thoại. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
 */
public class Dialog implements DialogInterface {
    private final Context mContext;
    private boolean mShowing = false;
    private boolean mCancelable = true;
    private OnDismissListener mDismissListener;
    private OnCancelListener mCancelListener;

    public Dialog(Context context) {
        mContext = context;
    }

    public Dialog(Context context, int themeResId) {
        mContext = context;
    }

    /**
     * trả về bối cảnh mà hộp thoại này được tạo.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * hiển thị hộp thoại.
     */
    public void show() {
        mShowing = true;
    }

    /**
     * bỏ qua hộp thoại.
     */
    public void dismiss() {
        mShowing = false;
        if (mDismissListener != null) {
            mDismissListener.onDismiss(this);
        }
    }

    /**
     * hủy hộp thoại.
     */
    public void cancel() {
        mShowing = false;
        if (mCancelListener != null) {
            mCancelListener.onCancel(this);
        }
    }

    /**
     * trả về việc hộp thoại có đang hiển thị hay không.
     */
    public boolean isShowing() {
        return mShowing;
    }

    /**
     * thiết lập xem hộp thoại có thể hủy được hay không.
     */
    public void setCancelable(boolean flag) {
        mCancelable = flag;
    }

    /**
     * trả về việc hộp thoại có thể hủy được hay không.
     */
    public boolean isCancelable() {
        return mCancelable;
    }

    /**
     * thiết lập trình lắng nghe bỏ qua.
     */
    public void setOnDismissListener(OnDismissListener listener) {
        mDismissListener = listener;
    }

    /**
     * thiết lập trình lắng nghe hủy.
     */
    public void setOnCancelListener(OnCancelListener listener) {
        mCancelListener = listener;
    }

    /**
     * thiết lập tiêu đề.
     */
    public void setTitle(CharSequence title) {
    }

    /**
     * thiết lập dạng xem nội dung.
     */
    public void setContentView(android.view.View view) {
    }

    /**
     * thiết lập dạng xem nội dung từ tài nguyên bố cục.
     */
    public void setContentView(int layoutResID) {
    }
}
