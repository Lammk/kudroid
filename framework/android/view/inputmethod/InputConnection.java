package android.view.inputmethod;

import android.os.Bundle;
import android.view.KeyEvent;

/**
 * The channel an IME uses to read and edit the text of a focused view.
 *
 * An interface, as in AOSP. It was a class here, which is a subtle mismatch: d8
 * compiles a call on an InputConnection-typed variable to invoke-interface, and an
 * app class declares {@code implements InputConnection}. Shipping it as a class made
 * the app's declaration land in the wrong table.
 */
public interface InputConnection {
    int GET_TEXT_WITH_STYLES = 0x0001;
    int GET_EXTRACTED_TEXT_MONITOR = 0x0001;

    CharSequence getTextBeforeCursor(int n, int flags);
    CharSequence getTextAfterCursor(int n, int flags);
    CharSequence getSelectedText(int flags);
    int getCursorCapsMode(int reqModes);
    ExtractedText getExtractedText(ExtractedTextRequest request, int flags);

    boolean deleteSurroundingText(int beforeLength, int afterLength);
    boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength);
    boolean setComposingText(CharSequence text, int newCursorPosition);
    boolean setComposingRegion(int start, int end);
    boolean finishComposingText();
    boolean commitText(CharSequence text, int newCursorPosition);
    boolean commitCompletion(CompletionInfo text);
    boolean setSelection(int start, int end);
    boolean performEditorAction(int editorAction);
    boolean performContextMenuAction(int id);
    boolean beginBatchEdit();
    boolean endBatchEdit();
    boolean sendKeyEvent(KeyEvent event);
    boolean clearMetaKeyStates(int states);
    boolean reportFullscreenMode(boolean enabled);
    boolean performPrivateCommand(String action, Bundle data);
    boolean requestCursorUpdates(int cursorUpdateMode);
    void closeConnection();
}
