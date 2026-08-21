package android.widget;

import android.content.Context;

/**
 * android.widget.EditText — TextView cho phép sửa nội dung.
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

    /** getText() của EditText trả Editable; bọc nội dung hiện tại. */
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
