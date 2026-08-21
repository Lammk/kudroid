package android.text;

/**
 * android.text.SpannableStringBuilder — Editable dựa trên StringBuilder.
 *
 * Chưa hỗ trợ span (KuDroid không có text styling), nhưng toàn bộ phần sửa nội
 * dung là thật nên app soạn thảo văn bản chạy đúng.
 */
public class SpannableStringBuilder implements Editable {
    private final StringBuilder mText;

    public SpannableStringBuilder() {
        mText = new StringBuilder();
    }

    public SpannableStringBuilder(CharSequence source) {
        mText = new StringBuilder(source == null ? "" : source.toString());
    }

    public SpannableStringBuilder(CharSequence source, int start, int end) {
        mText = new StringBuilder(source == null ? "" : source.subSequence(start, end).toString());
    }

    public int length() {
        return mText.length();
    }

    public char charAt(int index) {
        return mText.charAt(index);
    }

    public CharSequence subSequence(int start, int end) {
        return mText.subSequence(start, end);
    }

    @Override
    public String toString() {
        return mText.toString();
    }

    public Editable replace(int st, int en, CharSequence source, int start, int end) {
        final String piece = source == null ? "" : source.subSequence(start, end).toString();
        mText.replace(st, en, piece);
        return this;
    }

    public Editable replace(int st, int en, CharSequence text) {
        mText.replace(st, en, text == null ? "" : text.toString());
        return this;
    }

    public Editable insert(int where, CharSequence text, int start, int end) {
        return replace(where, where, text, start, end);
    }

    public Editable insert(int where, CharSequence text) {
        return replace(where, where, text);
    }

    public Editable delete(int st, int en) {
        mText.delete(st, en);
        return this;
    }

    public Editable append(CharSequence text) {
        mText.append(text == null ? "" : text);
        return this;
    }

    public Editable append(CharSequence text, int start, int end) {
        if (text != null) mText.append(text, start, end);
        return this;
    }

    public Editable append(char text) {
        mText.append(text);
        return this;
    }

    public void clear() {
        mText.setLength(0);
    }
}
