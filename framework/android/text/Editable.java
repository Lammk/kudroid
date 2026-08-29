package android.text;

/**
 * android.text.Editable — CharSequence can be edited in place.
 *
 * Android declares Editable extends CharSequence, GetChars, Spannable,
 *Appendable. KuDroid doesn't have Span yet so it's CharSequence + Appendable —
 * enough for plain text editing app (ZArchiver rename/password dialog).
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
     * Filters every insertion has to pass through.
     *
     * Part of the interface in AOSP, and libraries call it on an Editable they were
     * handed rather than on a concrete type — androidx's gametextinput caps its
     * editor's length that way. Auto-stubbing the call meant the cap was silently
     * dropped, so a length-limited field accepted unlimited text.
     */
    void setFilters(InputFilter[] filters);

    InputFilter[] getFilters();

    /**
     * Factory creates Editable from original content.
     */
    public interface Factory {
        Editable newEditable(CharSequence source);
    }
}
