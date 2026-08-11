package android.view.inputmethod;

/**
 * Stub android.view.inputmethod.InputMethodManager.
 *
 * Manages the input method (keyboard). For KuDroid's minimal framework, this
 * is a stub.
 */
public class InputMethodManager {
    /** Result: success. */
    public static final int RESULT_SUCCESS = 0;
    /** Result: shown. */
    public static final int RESULT_SHOWN = 1;
    /** Result: hidden. */
    public static final int RESULT_HIDDEN = 2;

    public InputMethodManager() {
    }

    public boolean showSoftInput(android.view.View view, int flags) {
        return false;
    }

    public boolean hideSoftInputFromWindow(android.os.IBinder windowToken, int flags) {
        return false;
    }

    public void toggleSoftInputFromWindow(android.os.IBinder windowToken, int showFlags, int hideFlags) {
    }

    public boolean isAcceptingText() {
        return false;
    }

    public boolean isActive() {
        return false;
    }
}