package android.view.inputmethod;

import android.os.Bundle;
import android.text.Editable;
import android.text.SpannableStringBuilder;
import android.view.KeyEvent;
import android.view.View;

/**
 * The default InputConnection an app subclasses to receive IME edits.
 *
 * Apps extend this and override only the few methods they care about — Minecraft's
 * does exactly that, overriding commitText and sendKeyEvent and inheriting the rest
 * — so every method here needs behaviour that is correct on its own, not a stub
 * returning zero. A getTextBeforeCursor that always returned null, for instance,
 * makes an IME believe the field is empty and re-send text the app already has.
 *
 * The editable buffer is real: composing text, deletion and selection all operate on
 * it. That keeps the two sides consistent even with no IME attached, which matters
 * because the app reads back what it wrote.
 *
 * This class was previously an auto-generated stub with a no-arg constructor only,
 * so {@code super(view, fullEditor)} in the app's subclass resolved to an auto-stub
 * and the connection was never wired to its view.
 */
public class BaseInputConnection implements InputConnection {

    private final View mTargetView;
    private final boolean mFullEditor;
    private final Editable mEditable;

    private int mComposingStart = -1;
    private int mComposingEnd = -1;
    private int mSelStart;
    private int mSelEnd;
    private int mBatchDepth;

    public BaseInputConnection(View targetView, boolean fullEditor) {
        mTargetView = targetView;
        mFullEditor = fullEditor;
        mEditable = new SpannableStringBuilder("");
    }

    /** Kept for callers that construct without a view. */
    public BaseInputConnection() {
        this(null, false);
    }

    public View getTargetView() { return mTargetView; }

    public boolean isFullEditor() { return mFullEditor; }

    /**
     * The buffer being edited.
     *
     * AOSP returns the target view's own Editable when there is one; KuDroid's View
     * has no text storage, so this connection owns the buffer. Subclasses that keep
     * their own text override the accessors instead.
     */
    public Editable getEditable() { return mEditable; }

    public static final int getComposingSpanStart(android.text.Spannable text) { return -1; }
    public static final int getComposingSpanEnd(android.text.Spannable text) { return -1; }

    private int length() {
        return mEditable != null ? mEditable.length() : 0;
    }

    private int clamp(int index) {
        if (index < 0) return 0;
        final int len = length();
        return index > len ? len : index;
    }

    @Override
    public CharSequence getTextBeforeCursor(int n, int flags) {
        if (n < 0 || mEditable == null) return "";
        final int end = clamp(mSelStart);
        final int start = end - n < 0 ? 0 : end - n;
        return mEditable.subSequence(start, end);
    }

    @Override
    public CharSequence getTextAfterCursor(int n, int flags) {
        if (n < 0 || mEditable == null) return "";
        final int start = clamp(mSelEnd);
        final int len = length();
        final int end = start + n > len ? len : start + n;
        return mEditable.subSequence(start, end);
    }

    @Override
    public CharSequence getSelectedText(int flags) {
        if (mEditable == null) return null;
        final int start = clamp(Math.min(mSelStart, mSelEnd));
        final int end = clamp(Math.max(mSelStart, mSelEnd));
        // AOSP returns null, not "", when nothing is selected; an IME distinguishes
        // the two to decide whether a commit replaces or inserts.
        if (start == end) return null;
        return mEditable.subSequence(start, end);
    }

    @Override
    public int getCursorCapsMode(int reqModes) { return 0; }

    @Override
    public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
        if (mEditable == null) return null;
        ExtractedText et = new ExtractedText();
        et.text = mEditable.toString();
        et.startOffset = 0;
        et.selectionStart = clamp(mSelStart);
        et.selectionEnd = clamp(mSelEnd);
        et.partialStartOffset = -1;
        et.partialEndOffset = -1;
        return et;
    }

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        if (mEditable == null) return false;
        if (beforeLength < 0 || afterLength < 0) return false;

        // Delete after the selection first: removing text before it would shift the
        // offsets the second delete depends on.
        final int len = length();
        int afterStart = clamp(mSelEnd);
        int afterEnd = afterStart + afterLength > len ? len : afterStart + afterLength;
        if (afterEnd > afterStart) mEditable.delete(afterStart, afterEnd);

        int beforeEnd = clamp(mSelStart);
        int beforeStart = beforeEnd - beforeLength < 0 ? 0 : beforeEnd - beforeLength;
        if (beforeEnd > beforeStart) {
            mEditable.delete(beforeStart, beforeEnd);
            final int removed = beforeEnd - beforeStart;
            mSelStart -= removed;
            mSelEnd -= removed;
        }
        mSelStart = clamp(mSelStart);
        mSelEnd = clamp(mSelEnd);
        return true;
    }

    @Override
    public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
        // No surrogate-pair handling: the buffer is UTF-16 and treating code points
        // as chars is wrong only for text outside the BMP.
        return deleteSurroundingText(beforeLength, afterLength);
    }

    @Override
    public boolean setComposingText(CharSequence text, int newCursorPosition) {
        if (mEditable == null || text == null) return false;
        replaceRange(composingOrSelection(), text);
        mComposingStart = mSelStart - text.length();
        mComposingEnd = mSelStart;
        applyRelativeCursor(newCursorPosition, text.length());
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
        if (mEditable == null || text == null) return false;
        replaceRange(composingOrSelection(), text);
        finishComposingText();
        applyRelativeCursor(newCursorPosition, text.length());
        return true;
    }

    @Override
    public boolean commitCompletion(CompletionInfo text) {
        if (text == null) return false;
        return commitText(text.getText(), 1);
    }

    @Override
    public boolean setSelection(int start, int end) {
        mSelStart = clamp(start);
        mSelEnd = clamp(end);
        return true;
    }

    @Override
    public boolean performEditorAction(int editorAction) { return true; }

    @Override
    public boolean performContextMenuAction(int id) { return false; }

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

    /**
     * Deliver a key event to the target view.
     *
     * AOSP routes it through the view hierarchy; KuDroid's View has no key
     * dispatch, so returning true reports it as handled rather than dropping it
     * silently. An app that overrides this — which is the usual reason to subclass —
     * never reaches here anyway.
     */
    @Override
    public boolean sendKeyEvent(KeyEvent event) { return event != null; }

    @Override
    public boolean clearMetaKeyStates(int states) { return true; }

    @Override
    public boolean reportFullscreenMode(boolean enabled) { return true; }

    @Override
    public boolean performPrivateCommand(String action, Bundle data) { return true; }

    @Override
    public boolean requestCursorUpdates(int cursorUpdateMode) { return false; }

    @Override
    public void closeConnection() {
        finishComposingText();
    }

    /** The composing span if one is active, otherwise the selection. */
    private int[] composingOrSelection() {
        if (mComposingStart >= 0 && mComposingEnd >= mComposingStart) {
            return new int[]{clamp(mComposingStart), clamp(mComposingEnd)};
        }
        return new int[]{clamp(Math.min(mSelStart, mSelEnd)),
                         clamp(Math.max(mSelStart, mSelEnd))};
    }

    private void replaceRange(int[] range, CharSequence text) {
        mEditable.replace(range[0], range[1], text);
        mSelStart = range[0] + text.length();
        mSelEnd = mSelStart;
    }

    /**
     * Move the cursor the way commitText/setComposingText define it.
     *
     * Positive newCursorPosition counts forward from the END of the inserted text,
     * zero or negative counts back from its start. Getting this backwards leaves the
     * cursor a whole word away from where the IME expects it.
     */
    private void applyRelativeCursor(int newCursorPosition, int insertedLength) {
        if (newCursorPosition > 0) {
            final int target = mSelStart + (newCursorPosition - 1);
            mSelStart = clamp(target);
        } else {
            final int start = mSelStart - insertedLength;
            mSelStart = clamp(start + newCursorPosition);
        }
        mSelEnd = mSelStart;
    }
}
