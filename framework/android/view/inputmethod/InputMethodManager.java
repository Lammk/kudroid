package android.view.inputmethod;

import android.content.Context;
import android.os.IBinder;
import android.os.ResultReceiver;
import android.view.View;

/**
 * The soft-keyboard manager.
 *
 * KuDroid has no keyboard of its own, so show/hide are forwarded to the host, which
 * raises the platform keyboard. Text typed there comes back in through
 * {@link #deliverText} and is applied to whichever InputConnection is currently
 * registered — see {@link #setCurrentInputConnection}.
 *
 * Every query answers optimistically ("yes, input is available"). That is the useful
 * shape rather than an honest "no": an app that sees {@code isActive()} return false,
 * or {@code showSoftInput()} return false, disables its own text entry outright,
 * whereas answering yes leaves the UI working even in the moment before the host has
 * a keyboard up.
 *
 * Obtained through {@code getSystemService(Context.INPUT_METHOD_SERVICE)}. That
 * mapping was missing while this class existed, which an app cannot tell apart from
 * the class being absent: Minecraft's onCreate threw
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

    /**
     * Where host keystrokes go.
     *
     * Set when a view is given focus for input, so that text lands in the app's own
     * connection rather than in a buffer nobody reads. Static because there is one
     * keyboard and one focused editor per process, and because the host delivers text
     * without any object to route it through.
     */
    private static InputConnection sCurrentConnection;
    private static View sCurrentView;

    private View mServedView;

    InputMethodManager() {}

    /**
     * The process-wide instance.
     *
     * AOSP keeps exactly one and apps depend on it: they cache what getSystemService
     * returned and expect later calls to reach the same object, so the view they
     * reported as focused is the one a subsequent showSoftInput acts on.
     * Context.getSystemService routes here rather than constructing.
     */
    public static synchronized InputMethodManager getInstance() {
        if (sInstance == null) sInstance = new InputMethodManager();
        return sInstance;
    }

    /** Deprecated in AOSP but still called by older game code. */
    public static InputMethodManager peekInstance() { return getInstance(); }

    // ── host bridge ─────────────────────────────────────────────────────────
    // Implemented in src/kudroid_bridge.cpp; forwarded to the platform keyboard.

    private static native boolean showSoftInputNative(int flags);
    private static native boolean hideSoftInputNative();
    private static native boolean isSoftInputVisibleNative();

    /**
     * Register the connection host keystrokes should be applied to.
     *
     * Called from View focus handling rather than by the app. The EditorInfo the view
     * filled in is kept alongside, because an IME reads inputType and imeOptions from
     * it to decide what kind of keyboard to show — the host does the same.
     */
    public static synchronized void setCurrentInputConnection(View view,
                                                             InputConnection connection) {
        sCurrentView = view;
        sCurrentConnection = connection;
    }

    public static synchronized InputConnection getCurrentInputConnection() {
        return sCurrentConnection;
    }

    /**
     * Apply text typed on the host keyboard.
     *
     * Called from ActivityThread on the Looper thread (see postTextInput), never
     * directly from the host: the connection edits the same buffer the app's UI reads.
     *
     * A no-op when nothing is focused. Silently dropping is right here — the
     * alternative, buffering keystrokes for a future editor, delivers them to the
     * wrong field later.
     */
    public static void deliverText(String text) {
        if (text == null || text.isEmpty()) return;
        final InputConnection ic;
        synchronized (InputMethodManager.class) {
            ic = sCurrentConnection;
        }
        if (ic == null) {
            android.util.Log.w("InputMethodManager",
                    "text typed with no focused InputConnection, dropped: \"" + text + "\"");
            return;
        }
        try {
            // newCursorPosition = 1: the cursor follows the inserted text, which is
            // what a keyboard insertion means.
            ic.commitText(text, 1);
        } catch (Throwable t) {
            android.util.Log.e("InputMethodManager", "commitText threw: " + t.toString());
        }
    }

    /** Apply a backspace from the host keyboard. */
    public static void deliverDeleteBackward() {
        final InputConnection ic;
        synchronized (InputMethodManager.class) {
            ic = sCurrentConnection;
        }
        if (ic == null) return;
        try {
            ic.deleteSurroundingText(1, 0);
        } catch (Throwable t) {
            android.util.Log.e("InputMethodManager",
                    "deleteSurroundingText threw: " + t.toString());
        }
    }

    // ── the IME API apps call ───────────────────────────────────────────────

    public boolean isActive(View view) {
        return view != null;
    }

    public boolean isActive() { return true; }

    public boolean isAcceptingText() { return true; }

    /**
     * True while the host keyboard is on screen.
     *
     * Distinct from isActive(), which reports whether input is possible at all. Apps
     * use this one to lay out around the keyboard.
     */
    public boolean isFullscreenMode() { return false; }

    public boolean isSoftInputVisible() {
        try {
            return isSoftInputVisibleNative();
        } catch (Throwable t) {
            return false;
        }
    }

    /**
     * Raise the keyboard for `view`.
     *
     * Returns true even when the host has no keyboard to show. That is deliberate: the
     * return value is read as "does this platform support text input", and a false
     * makes an app disable its text UI permanently rather than retry.
     */
    public boolean showSoftInput(View view, int flags) {
        if (view != null) {
            mServedView = view;
            attach(view);
        }
        try {
            showSoftInputNative(flags);
        } catch (Throwable t) {
            android.util.Log.w("InputMethodManager",
                    "showSoftInput could not reach the host: " + t.toString());
        }
        return true;
    }

    public boolean showSoftInput(View view, int flags, ResultReceiver resultReceiver) {
        return showSoftInput(view, flags);
    }

    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags) {
        try {
            hideSoftInputNative();
        } catch (Throwable t) {
            android.util.Log.w("InputMethodManager",
                    "hideSoftInputFromWindow could not reach the host: " + t.toString());
        }
        return true;
    }

    public boolean hideSoftInputFromWindow(IBinder windowToken, int flags,
                                          ResultReceiver resultReceiver) {
        return hideSoftInputFromWindow(windowToken, flags);
    }

    public void toggleSoftInput(int showFlags, int hideFlags) {
        if (isSoftInputVisible()) {
            hideSoftInputFromWindow(null, hideFlags);
        } else {
            showSoftInput(mServedView, showFlags);
        }
    }

    public void toggleSoftInputFromWindow(IBinder windowToken, int showFlags, int hideFlags) {
        toggleSoftInput(showFlags, hideFlags);
    }

    /**
     * Ask the view for a fresh InputConnection and make it the current target.
     *
     * This is the step that decides where typed text lands. Without it the host's
     * keystrokes reach whatever connection was registered last — or nothing at all,
     * which is why showSoftInput does it rather than leaving it to the app.
     */
    private void attach(View view) {
        if (view == null) return;
        try {
            EditorInfo attrs = new EditorInfo();
            attrs.packageName = view.getContext() != null
                    ? view.getContext().getPackageName() : null;
            InputConnection ic = view.onCreateInputConnection(attrs);
            if (ic != null) setCurrentInputConnection(view, ic);
        } catch (Throwable t) {
            android.util.Log.e("InputMethodManager",
                    "onCreateInputConnection threw: " + t.toString());
        }
    }

    /**
     * The app changed the view's input state; re-read its connection.
     *
     * Apps call this after switching what a field accepts, and expect the next
     * keystroke to go to the new connection. Ignoring it leaves text going to the old
     * one.
     */
    public void restartInput(View view) {
        if (view == null) return;
        mServedView = view;
        attach(view);
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
