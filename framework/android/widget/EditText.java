package android.widget;

import android.content.Context;
import android.view.MotionEvent;
import android.view.inputmethod.InputMethodManager;

/**
 * android.widget.EditText — TextView allows editing content.
 *
 * The text is a live Editable (see TextView): edits from app code and from the host
 * keyboard both reach it, fire TextWatchers and move the cursor. A tap focuses the
 * field and raises the keyboard.
 */
public class EditText extends TextView {

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

    @Override
    protected boolean isEditable() {
        return true;
    }

    /**
     * The text, as an Editable.
     *
     * EditText NARROWS TextView's return type: apps reference
     * {@code EditText.getText()Landroid/text/Editable;} and immediately call editing methods
     * on it. It is the view's own buffer, so an edit made through it is the edit.
     */
    @Override
    public android.text.Editable getText() {
        return getEditableBuffer();
    }

    public android.text.Editable getEditableText() {
        return getEditableBuffer();
    }

    public void setSelection(int index) {
        setSelectionRange(index, index);
    }

    public void setSelection(int start, int stop) {
        setSelectionRange(start, stop);
    }

    public void selectAll() {
        setSelectionRange(0, length());
    }

    public void extendSelection(int index) {
        setSelectionRange(getSelectionStart(), index);
    }

    /**
     * A tap focuses the field, puts the cursor after the text and raises the keyboard.
     *
     * DOWN is claimed so the matching UP reaches this view; without that the parent
     * drops the gesture and the keyboard never appears.
     */
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (event == null) return false;
        final int action = event.getAction();
        if (action == MotionEvent.ACTION_DOWN) {
            super.onTouchEvent(event);
            return true;
        }
        if (action == MotionEvent.ACTION_UP) {
            super.onTouchEvent(event);
            requestFocus();
            setSelectionRange(length(), length());
            InputMethodManager.getInstance().showSoftInput(this, 0);
            return true;
        }
        return super.onTouchEvent(event);
    }
}
