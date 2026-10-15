package android.widget;

import android.content.Context;
import android.view.View;

public class ProgressBar extends View {
    private int mProgress = 0;
    private int mMax = 100;
    private boolean mIndeterminate = false;

    public ProgressBar(Context context) { super(context); }
    public synchronized void setIndeterminate(boolean indeterminate) { mIndeterminate = indeterminate; }
    public synchronized boolean isIndeterminate() { return mIndeterminate; }
    public synchronized int getProgress() { return mProgress; }
    public synchronized void setProgress(int progress) { mProgress = Math.max(0, Math.min(progress, mMax)); }
    public synchronized int getMax() { return mMax; }
    public synchronized void setMax(int max) { mMax = max; }
}
