package android.widget;

import android.content.Context;

/**
 * android.widget.EditText — TextView allows editing content.
 */
public class EditText extends TextView {
    private CharSequence mHint = "";

    public EditText(Context context) {
        super(context);
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

    /** getText() of EditText returns Editable; wrap current content. */
    public android.text.Editable getEditableText() {
        return new android.text.SpannableStringBuilder(getText());
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
