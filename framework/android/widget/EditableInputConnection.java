package android.widget;

import android.os.Bundle;
import android.text.Editable;
import android.view.KeyEvent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.CompletionInfo;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;

/**
 * The InputConnection of a TextView: every edit lands in the view's own Editable.
 *
 * Text and deletions from the host keyboard arrive here. Return is decided the way a
 * device's keyboard decides it: a single-line field turns it into an editor action
 * (Done/Search/...), a multi-line one inserts a line break.
 */
final class EditableInputConnection extends BaseInputConnection {
    private final TextView mView;
    private int mComposingStart = -1;
    private int mComposingEnd = -1;
    private int mBatchDepth;

    EditableInputConnection(TextView view) {
        super(view, true);
        mView = view;
    }

    private Editable buffer() {
        return mView.getEditableBuffer();
    }

    private int clamp(int index) {
        final int len = buffer().length();
        return index < 0 ? 0 : (index > len ? len : index);
    }

    private int selStart() {
        return clamp(Math.min(mView.getSelectionStart(), mView.getSelectionEnd()));
    }

    private int selEnd() {
        return clamp(Math.max(mView.getSelectionStart(), mView.getSelectionEnd()));
    }

    @Override
    public Editable getEditable() {
        return buffer();
    }

    @Override
    public CharSequence getTextBeforeCursor(int n, int flags) {
        if (n < 0) return "";
        final int end = selStart();
        return buffer().subSequence(Math.max(0, end - n), end);
    }

    @Override
    public CharSequence getTextAfterCursor(int n, int flags) {
        if (n < 0) return "";
        final int start = selEnd();
        return buffer().subSequence(start, Math.min(buffer().length(), start + n));
    }

    @Override
    public CharSequence getSelectedText(int flags) {
        final int start = selStart();
        final int end = selEnd();
        // null, not "": an IME tells "nothing selected" from "empty selection" by it.
        return start == end ? null : buffer().subSequence(start, end);
    }

    @Override
    public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
        final ExtractedText et = new ExtractedText();
        et.text = buffer().toString();
        et.startOffset = 0;
        et.selectionStart = selStart();
        et.selectionEnd = selEnd();
        et.partialStartOffset = -1;
        et.partialEndOffset = -1;
        return et;
    }

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        if (beforeLength < 0 || afterLength < 0) return false;
        final Editable b = buffer();
        // After the selection first, so removing text before it cannot shift it.
        final int end = selEnd();
        if (afterLength > 0 && end < b.length()) {
            b.delete(end, Math.min(b.length(), end + afterLength));
        }
        final int start = selStart();
        if (beforeLength > 0 && start > 0) {
            b.delete(Math.max(0, start - beforeLength), start);
        }
        return true;
    }

    @Override
    public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
        return deleteSurroundingText(beforeLength, afterLength);
    }

    /** Replace the composing region, or the selection when nothing is composing. */
    private int replaceTarget(CharSequence text) {
        final Editable b = buffer();
        int start;
        int end;
        if (mComposingStart >= 0 && mComposingEnd >= mComposingStart) {
            start = clamp(mComposingStart);
            end = clamp(mComposingEnd);
        } else {
            start = selStart();
            end = selEnd();
        }
        final int before = b.length();
        b.replace(start, end, text);
        final int inserted = b.length() - (before - (end - start));
        mView.setSelectionRange(start + inserted, start + inserted);
        return start;
    }

    @Override
    public boolean setComposingText(CharSequence text, int newCursorPosition) {
        if (text == null) return false;
        final int start = replaceTarget(text);
        mComposingStart = start;
        mComposingEnd = mView.getSelectionStart();
        applyCursor(newCursorPosition, start, mComposingEnd);
        return true;
    }

    @Override
    public boolean setComposingRegion(int start, int end) {
        mComposingStart = clamp(Math.min(start, end));
        mComposingEnd = clamp(Math.max(start, end));
        return true;
    }

    @Override
    public boolean finishComposingText() {
        mComposingStart = -1;
        mComposingEnd = -1;
        return true;
    }

    @Override
    public boolean commitText(CharSequence text, int newCursorPosition) {
        if (text == null) return false;
        final int start = replaceTarget(text);
        final int end = mView.getSelectionStart();
        finishComposingText();
        applyCursor(newCursorPosition, start, end);
        return true;
    }

    /** Positive counts forward from the end of the inserted text, else back from its start. */
    private void applyCursor(int newCursorPosition, int start, int end) {
        final int pos = newCursorPosition > 0 ? end + newCursorPosition - 1
                                              : start + newCursorPosition;
        mView.setSelectionRange(clamp(pos), clamp(pos));
    }

    @Override
    public boolean commitCompletion(CompletionInfo text) {
        return text != null && commitText(text.getText(), 1);
    }

    @Override
    public boolean setSelection(int start, int end) {
        mView.setSelectionRange(start, end);
        return true;
    }

    @Override
    public boolean performEditorAction(int editorAction) {
        mView.onEditorAction(editorAction);
        return true;
    }

    @Override
    public boolean beginBatchEdit() {
        ++mBatchDepth;
        return true;
    }

    @Override
    public boolean endBatchEdit() {
        if (mBatchDepth == 0) return false;
        --mBatchDepth;
        return mBatchDepth > 0;
    }

    /** Return and Delete from the keyboard; other keys are not text and are not handled. */
    @Override
    public boolean sendKeyEvent(KeyEvent event) {
        if (event == null) return false;
        final int code = event.getKeyCode();
        if (code != KeyEvent.KEYCODE_ENTER && code != KeyEvent.KEYCODE_DEL) return false;
        if (event.getAction() != KeyEvent.ACTION_DOWN) return true;
        if (code == KeyEvent.KEYCODE_DEL) {
            if (selStart() != selEnd()) return commitText("", 1);
            return deleteSurroundingText(1, 0);
        }
        return enter();
    }

    private boolean enter() {
        final int imeOptions = mView.getImeOptions();
        int action = imeOptions & EditorInfo.IME_MASK_ACTION;
        final boolean hasAction = action != EditorInfo.IME_ACTION_UNSPECIFIED
                && action != EditorInfo.IME_ACTION_NONE;
        if (!mView.isMultiLineInput()) {
            mView.onEditorAction(hasAction ? action : EditorInfo.IME_ACTION_DONE);
            return true;
        }
        if (hasAction && (imeOptions & EditorInfo.IME_FLAG_NO_ENTER_ACTION) == 0) {
            mView.onEditorAction(action);
            return true;
        }
        return commitText("\n", 1);
    }

    @Override
    public boolean performPrivateCommand(String action, Bundle data) {
        return false;
    }

    @Override
    public void closeConnection() {
        finishComposingText();
    }
}
