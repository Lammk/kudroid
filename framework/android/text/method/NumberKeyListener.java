package android.text.method;

import android.text.InputFilter;
import android.text.Spanned;

public abstract class NumberKeyListener extends BaseKeyListener implements InputFilter {
    protected abstract char[] getAcceptedChars();
    public CharSequence filter(CharSequence source, int start, int end, Spanned dest, int dstart, int dend) {
        return null;
    }
}
