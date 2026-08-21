package android.text;

/**
 * android.text.Editable — CharSequence có thể sửa tại chỗ.
 *
 * Android khai báo Editable extends CharSequence, GetChars, Spannable,
 * Appendable. KuDroid chưa có Span nên thu về CharSequence + Appendable —
 * đủ cho app soạn thảo văn bản thuần (ZArchiver rename/password dialog).
 */
public interface Editable extends CharSequence, Appendable {
    Editable replace(int st, int en, CharSequence source, int start, int end);

    Editable replace(int st, int en, CharSequence text);

    Editable insert(int where, CharSequence text, int start, int end);

    Editable insert(int where, CharSequence text);

    Editable delete(int st, int en);

    Editable append(CharSequence text);

    Editable append(CharSequence text, int start, int end);

    Editable append(char text);

    void clear();

    /**
     * Factory tạo Editable từ nội dung ban đầu.
     */
    public interface Factory {
        Editable newEditable(CharSequence source);
    }
}
