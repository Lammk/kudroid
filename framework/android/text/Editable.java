package android.text;

/** Mutable text with span and character access. */
public interface Editable extends GetChars, Spannable, Appendable {
    Editable replace(int st, int en, CharSequence source, int start, int end);

    Editable replace(int st, int en, CharSequence text);

    Editable insert(int where, CharSequence text, int start, int end);

    Editable insert(int where, CharSequence text);

    Editable delete(int st, int en);

    Editable append(CharSequence text);

    Editable append(CharSequence text, int start, int end);

    Editable append(char text);

    void clear();
    void clearSpans();

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
    public static class Factory {
        private static Editable.Factory sInstance = new Editable.Factory();

        public static Editable.Factory getInstance() {
            return sInstance;
        }

        public Editable newEditable(CharSequence source) {
            return new SpannableStringBuilder(source);
        }
    }
}
