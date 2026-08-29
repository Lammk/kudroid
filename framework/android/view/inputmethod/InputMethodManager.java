package android.view.inputmethod;

import android.content.Context;
import android.os.IBinder;
import android.os.ResultReceiver;
import android.view.View;

/**
 * The soft-keyboard manager.
 *
 * KuDroid has no on-screen keyboard of its own yet, so every query answers "yes,
 * input is available" and every request to show or hide succeeds without doing
 * anything. That is the useful shape: an app blocked on {@code isActive()} returning
 * false, or on {@code showSoftInput()} returning false, would disable its text entry
 * entirely, whereas answering optimistically leaves the UI functional and only the
 * keyboard itself absent.
 *
 * Obtained through {@code getSystemService(Context.INPUT_METHOD_SERVICE)}. That
 * mapping was missing while this class existed, which is indistinguishable from the
 * class not existing: Minecraft's onCreate threw
 * {@code RuntimeException("Can't get IMM")} and the whole Activity launch failed.
 */
public final class InputMethodManager {
    public static final int SHOW_IMPLICIT = 0x0001;
    public static final int SHOW_FORCED = 0x0002;
    public static final int HIDE_IMPLICIT_ONLY = 0x0001;
    public static final int HIDE_NOT_ALWAYS = 0x0002;

    public static final int RESULT_UNCHANGED_SHOWN = 0;
    public static final int RESULT_UNCHANGED_HIDDEN = 1;
    public static final int RESULT_SHOWN = 2;
    public static final int RESULT_HIDDEN = 3;

    private static InputMethodManager sInstance;

    private View mServedView;

    InputMethodManager() {}

    /**
     * The process-wide instance.
     *
     * AOSP keeps exactly one and apps depend on it: they cache what
     * getSystemService returned and expect later calls to reach the same object, so
     * that the view they reported as focused is the one a subsequent showSoftInput
     * acts on. Context.getSystemService routes here rather than constructing.
     */
    public static synchronized InputMethodManager getInstance() {
        if (sInstance == null) sInstance = new InputMethodManager();
        return sInstance;
    }

    /** Deprecated in AOSP but still called by older game code. */
    public static InputMethodManager peekInstance() { return getInstance(); }

    public boolean isActive(View view) {
        return view != null;
    }

    public boolean isActive() { return true; }

    public boolean isAcceptingText() { return true; }

    public boolean isFullscreenMode() { return false; }

    public boolean showSoftInput(View view, int flags) {
        if (view != null) mServedView = view;
        return true;
    }

    public boolean showSoftInput(View view, int flags, ResultReceiver resultReceiver) {
        return showSoftInput(view, flags);
    }

    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags) { return true; }

    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags,
                                          ResultReceiver resultReceiver) {
        return true;
    }

    public void toggleSoftInput(int showFlags, int hideFlags) {}

    public void toggleSoftInputFromWindow(IBinder windowToken, int showFlags, int hideFlags) {}

    /**
     * Tell the IME the view's input state changed.
     *
     * Records the view so isActive(view) stays consistent with what the app last
     * reported; the IME side has nothing to restart.
     */
    public void restartInput(View view) {
        if (view != null) mServedView = view;
    }

    public void displayCompletions(View view, CompletionInfo[] completions) {}

    public void updateExtractedText(View view, int token, ExtractedText text) {}

    public void updateSelection(View view, int selStart, int selEnd,
                                int candidatesStart, int candidatesEnd) {}

    public void updateCursorAnchorInfo(View view, CursorAnchorInfo cursorAnchorInfo) {}

    public void viewClicked(View view) {}

    public void sendAppPrivateCommand(View view, String action, android.os.Bundle data) {}

    public void showStatusIcon(IBinder imeToken, String packageName, int iconId) {}

    public void hideStatusIcon(IBinder imeToken) {}

    /** No IME is attached, so nothing can be watching a view. */
    public boolean isWatchingCursor(View view) { return false; }

    public java.util.List<InputMethodInfo> getInputMethodList() {
        return java.util.Collections.emptyList();
    }

    public java.util.List<InputMethodInfo> getEnabledInputMethodList() {
        return java.util.Collections.emptyList();
    }
}
