package android.content;

public abstract class BroadcastReceiver {
    private boolean mInitialStickyHint;
    public BroadcastReceiver() {}
    public abstract void onReceive(Context context, Intent intent);
    public final void abortBroadcast() {}
    public final boolean isInitialStickyBroadcast() { return mInitialStickyHint; }
}
