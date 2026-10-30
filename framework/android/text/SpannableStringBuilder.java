package android.text;

/** Editable text retaining span ranges through insertions and deletions. */
public class SpannableStringBuilder implements Editable, GetChars {
    private final StringBuilder mText;
    private final SpanStore mSpans = new SpanStore();

    public SpannableStringBuilder() {
        mText = new StringBuilder();
    }

    public SpannableStringBuilder(CharSequence source) {
        this(source, 0, source.length());
    }

    public SpannableStringBuilder(CharSequence source, int start, int end) {
        SpanStore.checkRange(start, end, source.length());
        mText = new StringBuilder(source.toString().substring(start, end));
        mSpans.copy(source, start, end, 0, true, this);
    }
    public static SpannableStringBuilder valueOf(CharSequence source) {
        return source instanceof SpannableStringBuilder ? (SpannableStringBuilder) source : new SpannableStringBuilder(source);
    }

    public int length() {
        return mText.length();
    }

    public char charAt(int index) {
        return mText.charAt(index);
    }

    public CharSequence subSequence(int start, int end) {
        return new SpannableStringBuilder(this, start, end);
    }
    public void getChars(int start, int end, char[] dest, int destoff) { mText.toString().getChars(start, end, dest, destoff); }

    @Override
    public String toString() {
        return mText.toString();
    }

    public SpannableStringBuilder replace(int st, int en, CharSequence source, int start, int end) {
        SpanStore.checkRange(st, en, length());
        SpanStore.checkRange(start, end, source.length());
        CharSequence piece = source.subSequence(start, end);
        piece = applyFilters(piece, st, en);
        if (piece == this) piece = new SpannedString(piece);
        mSpans.replace(st, en, piece.length(), this);
        mText.replace(st, en, piece.toString());
        mSpans.copy(piece, 0, piece.length(), st, true, this);
        return this;
    }

    public SpannableStringBuilder replace(int st, int en, CharSequence text) {
        return replace(st, en, text, 0, text.length());
    }

    public SpannableStringBuilder insert(int where, CharSequence text, int start, int end) {
        return replace(where, where, text, start, end);
    }

    public SpannableStringBuilder insert(int where, CharSequence text) {
        return replace(where, where, text);
    }

    public SpannableStringBuilder delete(int st, int en) {
        return replace(st, en, "");
    }

    @Override
    public SpannableStringBuilder append(CharSequence text) {
        if (text == null) text = "null";
        return replace(mText.length(), mText.length(), text);
    }

    @Override
    public SpannableStringBuilder append(CharSequence text, int start, int end) {
        if (text == null) text = "null";
        return replace(mText.length(), mText.length(), text, start, end);
    }

    @Override
    public SpannableStringBuilder append(char text) {
        return append(String.valueOf(text));
    }

    public void clear() {
        replace(0, length(), "");
    }
    public void clearSpans() { mSpans.clear(); }

    // Filters.

    private InputFilter[] mFilters = new InputFilter[0];

    @Override
    public void setFilters(InputFilter[] filters) {
        mFilters = filters != null ? filters : new InputFilter[0];
    }

    @Override
    public InputFilter[] getFilters() {
        return mFilters;
    }

    /** Run an insertion through the filter chain. */
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

    // Spans.

    @Override
    public void setSpan(Object what, int start, int end, int flags) {
        SpanStore.checkRange(start, end, length());
        if (start == end && (flags & Spanned.SPAN_POINT_MARK_MASK) == Spanned.SPAN_EXCLUSIVE_EXCLUSIVE) return;
        mSpans.set(this, what, start, end, flags);
    }

    @Override
    public void removeSpan(Object what) { mSpans.remove(what); }

    @Override
    public <T> T[] getSpans(int start, int end, Class<T> type) {
        return mSpans.spans(start, end, type);
    }

    @Override
    public int getSpanStart(Object tag) { return mSpans.start(tag); }

    @Override
    public int getSpanEnd(Object tag) { return mSpans.end(tag); }

    @Override
    public int getSpanFlags(Object tag) { return mSpans.flags(tag); }

    @Override
    public int nextSpanTransition(int start, int limit, Class type) {
        return mSpans.transition(start, limit, type);
    }
}
