package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

/**
 * Android ProgressDialog implementation.
 */
public class ProgressDialog extends AlertDialog {
    public static final int STYLE_SPINNER = 0;
    public static final int STYLE_HORIZONTAL = 1;

    private ProgressBar mProgress;
    private TextView mMessageView;
    private int mProgressStyle = STYLE_SPINNER;
    private int mMax = 100;
    private int mProgressVal = 0;
    private int mSecondaryProgressVal = 0;
    private Drawable mProgressDrawable;
    private Drawable mIndeterminateDrawable;
    private CharSequence mMessage;
    private boolean mIndeterminate;

    public ProgressDialog(Context context) {
        super(context);
    }

    public ProgressDialog(Context context, int theme) {
        super(context, theme);
    }

    public static ProgressDialog show(Context context, CharSequence title, CharSequence message) {
        return show(context, title, message, false);
    }

    public static ProgressDialog show(Context context, CharSequence title, CharSequence message, boolean indeterminate) {
        return show(context, title, message, indeterminate, false, null);
    }

    public static ProgressDialog show(Context context, CharSequence title, CharSequence message, boolean indeterminate, boolean cancelable) {
        return show(context, title, message, indeterminate, cancelable, null);
    }

    public static ProgressDialog show(Context context, CharSequence title, CharSequence message, boolean indeterminate,
                                      boolean cancelable, OnCancelListener cancelListener) {
        ProgressDialog dialog = new ProgressDialog(context);
        dialog.setTitle(title);
        dialog.setMessage(message);
        dialog.setIndeterminate(indeterminate);
        dialog.setCancelable(cancelable);
        dialog.setOnCancelListener(cancelListener);
        dialog.show();
        return dialog;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Context context = getContext();
        float density = 1.0f;
        if (context.getResources() != null && context.getResources().getDisplayMetrics() != null) {
            density = context.getResources().getDisplayMetrics().density;
        }

        LinearLayout root = new LinearLayout(context);
        root.setOrientation(mProgressStyle == STYLE_HORIZONTAL ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
        root.setGravity(Gravity.CENTER_VERTICAL);
        root.setPadding((int) (24 * density), (int) (16 * density), (int) (24 * density), (int) (16 * density));

        mProgress = new ProgressBar(context);
        mProgress.setMax(mMax);
        mProgress.setProgress(mProgressVal);
        mProgress.setSecondaryProgress(mSecondaryProgressVal);
        mProgress.setIndeterminate(mIndeterminate);

        mMessageView = new TextView(context);
        mMessageView.setText(mMessage);
        mMessageView.setTextSize(16.0f);

        if (mProgressStyle == STYLE_HORIZONTAL) {
            root.addView(mMessageView);
            root.addView(mProgress, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        } else {
            root.addView(mProgress, new LinearLayout.LayoutParams((int) (40 * density), (int) (40 * density)));
            mMessageView.setPadding((int) (16 * density), 0, 0, 0);
            root.addView(mMessageView);
        }

        setView(root);
        super.onCreate(savedInstanceState);
    }

    public void setProgress(int value) {
        mProgressVal = value;
        if (mProgress != null) {
            mProgress.setProgress(value);
        }
    }

    public void setSecondaryProgress(int secondaryProgress) {
        mSecondaryProgressVal = secondaryProgress;
        if (mProgress != null) {
            mProgress.setSecondaryProgress(secondaryProgress);
        }
    }

    public int getProgress() {
        return mProgress != null ? mProgress.getProgress() : mProgressVal;
    }

    public int getSecondaryProgress() {
        return mProgress != null ? mProgress.getSecondaryProgress() : mSecondaryProgressVal;
    }

    public int getMax() {
        return mProgress != null ? mProgress.getMax() : mMax;
    }

    public void setMax(int max) {
        mMax = max;
        if (mProgress != null) {
            mProgress.setMax(max);
        }
    }

    public void incrementProgressBy(int diff) {
        setProgress(getProgress() + diff);
    }

    public void incrementSecondaryProgressBy(int diff) {
        setSecondaryProgress(getSecondaryProgress() + diff);
    }

    public void setProgressDrawable(Drawable d) {
        mProgressDrawable = d;
        if (mProgress != null) {
            mProgress.setProgressDrawable(d);
        }
    }

    public void setIndeterminateDrawable(Drawable d) {
        mIndeterminateDrawable = d;
        if (mProgress != null) {
            mProgress.setIndeterminateDrawable(d);
        }
    }

    public void setIndeterminate(boolean indeterminate) {
        mIndeterminate = indeterminate;
        if (mProgress != null) {
            mProgress.setIndeterminate(indeterminate);
        }
    }

    public boolean isIndeterminate() {
        return mProgress != null ? mProgress.isIndeterminate() : mIndeterminate;
    }

    @Override
    public void setMessage(CharSequence message) {
        mMessage = message;
        if (mMessageView != null) {
            mMessageView.setText(message);
        } else {
            super.setMessage(message);
        }
    }

    public void setProgressStyle(int style) {
        mProgressStyle = style;
    }

    public void setProgressNumberFormat(String format) {}
    public void setProgressPercentFormat(java.text.NumberFormat format) {}
}
