package android.widget;

import android.content.Context;

/**
 * android.widget.EditText — TextView allows editing content.
 */
public class EditText extends TextView {
    private CharSequence mHint = "";

    public EditText(Context context) {
        this(context, null);
    }

    public EditText(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public EditText(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public EditText(Context context, android.util.AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    public void setHint(CharSequence hint) {
        mHint = hint != null ? hint : "";
    }

    public void setHint(int resId) {
        mHint = "";
    }

    public CharSequence getHint() {
        return mHint;
    }

    /**
     * The text, as an Editable.
     *
     * EditText NARROWS TextView's return type: apps reference
     * {@code EditText.getText()Landroid/text/Editable;} and immediately call editing methods
     * on it. Inheriting TextView's CharSequence version left that reference unresolved — a
     * missing method on a method that was obviously present, needed by five of six corpus
     * APKs.
     *
     * Backed by the same SpannableStringBuilder each time, and written back through
     * setText, so an app that edits what it gets from here sees the change reflected. A
     * fresh wrapper per call would accept edits and silently discard them.
     */
    private android.text.SpannableStringBuilder mEditable;

    @Override
    public android.text.Editable getText() {
        final CharSequence current = super.getText();
        if (mEditable == null) {
            mEditable = new android.text.SpannableStringBuilder(current);
        } else if (!mEditable.toString().contentEquals(current)) {
            // setText was called behind our back; resynchronise rather than hand back stale
            // content.
            mEditable = new android.text.SpannableStringBuilder(current);
        }
        return mEditable;
    }

    public android.text.Editable getEditableText() {
        return getText();
    }

    public void setSelection(int index) {
    }

    public void setSelection(int start, int stop) {
    }

    public int getSelectionStart() {
        return getText().length();
    }

    public int getSelectionEnd() {
        return getText().length();
    }

    public void selectAll() {
    }

    public void setInputType(int type) {
    }

    public void setSingleLine(boolean singleLine) {
    }

    public void setSingleLine() {
    }
}
