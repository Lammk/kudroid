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

    private android.view.Window mWindow;

    /**
     * The dialog's window.
     *
     * Cached, for the same reason Activity.getWindow() is: apps store it, compare it, and
     * set things through it, and a fresh instance per call discards all three. Needed by
     * five of six corpus APKs — every library that dims a background or sizes a dialog goes
     * through getWindow().
     */
    public android.view.Window getWindow() {
        if (mWindow == null) mWindow = new android.view.Window(mContext);
        return mWindow;
    }

    /** Window features, requested before the content view exists. */
    public boolean requestWindowFeature(int featureId) {
        return getWindow().requestFeature(featureId);
    }

    public android.view.View findViewById(int id) {
        final android.view.View content = getWindow().getContentView();
        return content != null ? content.findViewById(id) : null;
    }

    /**
     * Attach/detach hooks, empty because KuDroid never gives a dialog its own window on
     * screen. Present because apps override them and the framework's own dispatch calls
     * them; an app whose override is never reached at least behaves consistently, while a
     * missing method is a crash during setup.
     */
    public void onAttachedToWindow() {
    }

    public void onDetachedFromWindow() {
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
     *
     * Routed through the window rather than dropped, so findViewById() below has a
     * hierarchy to search: a Dialog that accepts a content view and forgets it makes every
     * later lookup return null.
     */
    public void setContentView(android.view.View view) {
        getWindow().setContentView(view);
    }

    /**
     * set content view from layout resource.
     */
    public void setContentView(int layoutResID) {
        getWindow().setContentView(layoutResID);
    }
}
