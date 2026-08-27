package android.app;

import android.content.Context;
import android.content.DialogInterface;

/**
 * minimal android.app.dialog implementation.
 *
 * base class for dialogs. for kudroid minimal framework, here is an emulation.
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
     * returns the context in which this dialog was created.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * show dialog box.
     */
    public void show() {
        mShowing = true;
    }

    /**
     * skip dialog.
     */
    public void dismiss() {
        mShowing = false;
        if (mDismissListener != null) {
            mDismissListener.onDismiss(this);
        }
    }

    /**
     * cancel dialog.
     */
    public void cancel() {
        mShowing = false;
        if (mCancelListener != null) {
            mCancelListener.onCancel(this);
        }
    }

    /**
     * returns whether the dialog is visible or not.
     */
    public boolean isShowing() {
        return mShowing;
    }

    /**
     * sets whether the dialog is cancelable or not.
     */
    public void setCancelable(boolean flag) {
        mCancelable = flag;
    }

    /**
     * returns whether the dialog is cancelable or not.
     */
    public boolean isCancelable() {
        return mCancelable;
    }

    /**
     * set bypass listener.
     */
    public void setOnDismissListener(OnDismissListener listener) {
        mDismissListener = listener;
    }

    /**
     * set cancellation listener.
     */
    public void setOnCancelListener(OnCancelListener listener) {
        mCancelListener = listener;
    }

    /**
     * set title.
     */
    public void setTitle(CharSequence title) {
    }

    /**
     * set content view.
     */
    public void setContentView(android.view.View view) {
    }

    /**
     * set content view from layout resource.
     */
    public void setContentView(int layoutResID) {
    }
}
