package android.text;

/**
 * android.text.SpannableStringBuilder — Editable based on StringBuilder.
 *
 * Doesn't support span yet (KuDroid doesn't have text styling), but all the internals are fixed
 * The content is real so the text editor app runs properly.
 *
 * Implements Spanned as well as Editable even though no spans are stored, because
 * that is the type an InputFilter is handed as its `dest`: a filter has to be able to
 * ask how long the buffer already is, and LengthFilter's whole calculation depends on
 * it. AOSP has Editable extend Spanned; declaring it here keeps filters working
 * without pulling in span storage KuDroid has no use for yet.
 */
public class SpannableStringBuilder implements Editable, Spanned {
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
        CharSequence piece = source == null ? "" : source.subSequence(start, end);
        piece = applyFilters(piece, st, en);
        mText.replace(st, en, piece.toString());
        return this;
    }

    public Editable replace(int st, int en, CharSequence text) {
        CharSequence piece = text == null ? "" : text;
        piece = applyFilters(piece, st, en);
        mText.replace(st, en, piece.toString());
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
        // Through replace() rather than straight onto the builder, so an append is
        // filtered like any other insertion. Appending past a LengthFilter's cap would
        // otherwise bypass it entirely.
        return replace(mText.length(), mText.length(), text);
    }

    public Editable append(CharSequence text, int start, int end) {
        return replace(mText.length(), mText.length(), text, start, end);
    }

    public Editable append(char text) {
        return append(String.valueOf(text));
    }

    public void clear() {
        mText.setLength(0);
    }

    // ── filters ─────────────────────────────────────────────────────────────

    private InputFilter[] mFilters = new InputFilter[0];

    @Override
    public void setFilters(InputFilter[] filters) {
        mFilters = filters != null ? filters : new InputFilter[0];
    }

    @Override
    public InputFilter[] getFilters() {
        return mFilters;
    }

    /**
     * Run an insertion through the filter chain.
     *
     * Later filters see the output of earlier ones, which is what makes a length cap
     * placed after an upper-caser behave the way the app intended. A filter returning
     * null means "accept what you were given", so the running value is only replaced
     * on a non-null result.
     */
    private CharSequence applyFilters(CharSequence source, int dstart, int dend) {
        if (mFilters.length == 0) return source;
        CharSequence out = source;
        for (int i = 0; i < mFilters.length; ++i) {
            if (mFilters[i] == null) continue;
            CharSequence replacement =
                    mFilters[i].filter(out, 0, out.length(), this, dstart, dend);
            if (replacement != null) out = replacement;
        }
        return out;
    }

    // ── Spanned ─────────────────────────────────────────────────────────────
    //
    // No span storage: KuDroid has no text styling, so nothing can set one. The
    // methods answer "no spans here" rather than throwing, which is the correct
    // answer for a buffer that holds none — and it is what a filter or a layout pass
    // walking spans expects to find.

    @Override
    public <T> T[] getSpans(int start, int end, Class<T> type) {
        return (T[]) java.lang.reflect.Array.newInstance(type, 0);
    }

    @Override
    public int getSpanStart(Object tag) { return -1; }

    @Override
    public int getSpanEnd(Object tag) { return -1; }

    @Override
    public int getSpanFlags(Object tag) { return 0; }

    /**
     * The next index at which a span starts or ends.
     *
     * With no spans the only transition is the limit itself. Returning anything
     * smaller would make a caller loop forever, since it advances to the value this
     * returns and asks again.
     */
    @Override
    public int nextSpanTransition(int start, int limit, Class type) {
        return limit;
    }
}
