package android.view;

import android.content.Context;

public abstract class OrientationEventListener {
    public static final int ORIENTATION_UNKNOWN = -1;
    private boolean mEnabled = false;

    public OrientationEventListener(Context context) {}
    public OrientationEventListener(Context context, int rate) {}
    public void enable() { mEnabled = true; }
    public void disable() { mEnabled = false; }
    public boolean canDetectOrientation() { return true; }
    public abstract void onOrientationChanged(int orientation);
}
