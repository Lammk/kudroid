package android.view.inputmethod;

import android.content.Context;
import android.os.IBinder;
import android.os.ResultReceiver;
import android.view.View;

public final class InputMethodManager {
    public static final int SHOW_IMPLICIT = 0x0001;
    public static final int SHOW_FORCED = 0x0002;
    public static final int HIDE_IMPLICIT_ONLY = 0x0001;
    public static final int HIDE_NOT_ALWAYS = 0x0002;

    public static InputMethodManager peekInstance() { return new InputMethodManager(); }
    public boolean isActive(View view) { return true; }
    public boolean isActive() { return true; }
    public boolean isAcceptingText() { return true; }
    public boolean showSoftInput(View view, int flags) { return true; }
    public boolean showSoftInput(View view, int flags, ResultReceiver resultReceiver) { return true; }
    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags) { return true; }
    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags, ResultReceiver resultReceiver) { return true; }
    public void toggleSoftInput(int showFlags, int hideFlags) {}
    public void restartInput(View view) {}
}
