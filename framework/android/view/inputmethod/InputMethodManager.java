package android.view.inputmethod;

/**
 * emulate android.view.inputmethod.inputmethodmanager.
 *
 * manage input methods (keyboard). for kudroid minimal framework, here
 * is a simulation.
 */
public class InputMethodManager {
    /** result: success. */
    public static final int RESULT_SUCCESS = 0;
    /** result: displayed. */
    public static final int RESULT_SHOWN = 1;
    /** result: hidden. */
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