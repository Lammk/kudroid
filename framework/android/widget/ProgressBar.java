package android.widget;

import android.content.Context;
import android.view.View;

public class ProgressBar extends View {
    private int mProgress = 0;
    private int mMax = 100;
    private boolean mIndeterminate = false;

    private int mSecondaryProgress = 0;
    private android.graphics.drawable.Drawable mProgressDrawable;
    private android.graphics.drawable.Drawable mIndeterminateDrawable;

    public ProgressBar(Context context) { this(context, null); }
    public ProgressBar(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public ProgressBar(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public ProgressBar(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public synchronized void setIndeterminate(boolean indeterminate) { mIndeterminate = indeterminate; }
    public synchronized boolean isIndeterminate() { return mIndeterminate; }
    public synchronized int getProgress() { return mProgress; }
    public synchronized void setProgress(int progress) { mProgress = Math.max(0, Math.min(progress, mMax)); }
    public synchronized int getSecondaryProgress() { return mSecondaryProgress; }
    public synchronized void setSecondaryProgress(int secondaryProgress) { mSecondaryProgress = Math.max(0, Math.min(secondaryProgress, mMax)); }
    public synchronized int getMax() { return mMax; }
    public synchronized void setMax(int max) { mMax = max; }
    public void setProgressDrawable(android.graphics.drawable.Drawable d) { mProgressDrawable = d; }
    public android.graphics.drawable.Drawable getProgressDrawable() { return mProgressDrawable; }
    public void setIndeterminateDrawable(android.graphics.drawable.Drawable d) { mIndeterminateDrawable = d; }
    public android.graphics.drawable.Drawable getIndeterminateDrawable() { return mIndeterminateDrawable; }
}
