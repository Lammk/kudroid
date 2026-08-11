package android.app;

import android.content.Context;
import android.content.DialogInterface;

/**
 * triển khai android.app.alertdialog tối thiểu.
 *
 * một hộp thoại có tiêu đề, thông báo và các nút. đối với khuôn khổ
 * tối thiểu của kudroid, đây là một mô phỏng lưu trữ cấu hình.
 */
public class AlertDialog extends Dialog implements DialogInterface {
    private CharSequence mTitle;
    private CharSequence mMessage;
    private CharSequence mPositiveText;
    private CharSequence mNegativeText;
    private CharSequence mNeutralText;
    private OnClickListener mPositiveListener;
    private OnClickListener mNegativeListener;
    private OnClickListener mNeutralListener;

    protected AlertDialog(Context context) {
        super(context);
    }

    protected AlertDialog(Context context, int themeResId) {
        super(context);
    }

    /**
     * trả về tiêu đề.
     */
    public CharSequence getTitle() {
        return mTitle;
    }

    /**
     * đặt tiêu đề.
     */
    public void setTitle(CharSequence title) {
        mTitle = title;
    }

    /**
     * đặt thông báo.
     */
    public void setMessage(CharSequence message) {
        mMessage = message;
    }

    /**
     * thiết lập một nút tích cực.
     */
    public void setButton(int whichButton, CharSequence text, OnClickListener listener) {
        if (whichButton == BUTTON_POSITIVE) {
            mPositiveText = text;
            mPositiveListener = listener;
        } else if (whichButton == BUTTON_NEGATIVE) {
            mNegativeText = text;
            mNegativeListener = listener;
        } else if (whichButton == BUTTON_NEUTRAL) {
            mNeutralText = text;
            mNeutralListener = listener;
        }
    }

    /**
     * trình tạo để tạo một alertdialog.
     */
    public static class Builder {
        private final Context mContext;
        private final AlertDialog mDialog;

        public Builder(Context context) {
            mContext = context;
            mDialog = new AlertDialog(context);
        }

        public Builder setTitle(CharSequence title) {
            mDialog.setTitle(title);
            return this;
        }

        public Builder setTitle(int titleId) {
            mDialog.setTitle("");
            return this;
        }

        public Builder setMessage(CharSequence message) {
            mDialog.setMessage(message);
            return this;
        }

        public Builder setMessage(int messageId) {
            mDialog.setMessage("");
            return this;
        }

        public Builder setPositiveButton(CharSequence text, OnClickListener listener) {
            mDialog.setButton(BUTTON_POSITIVE, text, listener);
            return this;
        }

        public Builder setPositiveButton(int textId, OnClickListener listener) {
            mDialog.setButton(BUTTON_POSITIVE, "", listener);
            return this;
        }

        public Builder setNegativeButton(CharSequence text, OnClickListener listener) {
            mDialog.setButton(BUTTON_NEGATIVE, text, listener);
            return this;
        }

        public Builder setNegativeButton(int textId, OnClickListener listener) {
            mDialog.setButton(BUTTON_NEGATIVE, "", listener);
            return this;
        }

        public Builder setNeutralButton(CharSequence text, OnClickListener listener) {
            mDialog.setButton(BUTTON_NEUTRAL, text, listener);
            return this;
        }

        public Builder setCancelable(boolean cancelable) {
            mDialog.setCancelable(cancelable);
            return this;
        }

        public AlertDialog create() {
            return mDialog;
        }

        public AlertDialog show() {
            mDialog.show();
            return mDialog;
        }
    }
}
