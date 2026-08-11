package android.app;

import android.content.Context;
import android.content.DialogInterface;

/**
 * Minimal android.app.Dialog implementation.
 *
 * Base class for dialogs. For KuDroid's minimal framework, this is a stub.
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
     * Return the context this dialog was created with.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * Show the dialog.
     */
    public void show() {
        mShowing = true;
    }

    /**
     * Dismiss the dialog.
     */
    public void dismiss() {
        mShowing = false;
        if (mDismissListener != null) {
            mDismissListener.onDismiss(this);
        }
    }

    /**
     * Cancel the dialog.
     */
    public void cancel() {
        mShowing = false;
        if (mCancelListener != null) {
            mCancelListener.onCancel(this);
        }
    }

    /**
     * Return whether the dialog is showing.
     */
    public boolean isShowing() {
        return mShowing;
    }

    /**
     * Set whether the dialog is cancelable.
     */
    public void setCancelable(boolean flag) {
        mCancelable = flag;
    }

    /**
     * Return whether the dialog is cancelable.
     */
    public boolean isCancelable() {
        return mCancelable;
    }

    /**
     * Set the dismiss listener.
     */
    public void setOnDismissListener(OnDismissListener listener) {
        mDismissListener = listener;
    }

    /**
     * Set the cancel listener.
     */
    public void setOnCancelListener(OnCancelListener listener) {
        mCancelListener = listener;
    }

    /**
     * Set the title.
     */
    public void setTitle(CharSequence title) {
    }

    /**
     * Set the content view.
     */
    public void setContentView(android.view.View view) {
    }

    /**
     * Set the content view from a layout resource.
     */
    public void setContentView(int layoutResID) {
    }
}
