package android.text;

public class SpannableString implements Spannable, GetChars {
    private final String mText;
    private final SpanStore mSpans = new SpanStore();

    public SpannableString(CharSequence source) {
        this(source, false);
    }
    SpannableString(CharSequence source, boolean ignoreNoCopy) {
        if (source == null) throw new NullPointerException();
        mText = source.toString();
        mSpans.copy(source, 0, source.length(), 0, ignoreNoCopy);
    }
    public static SpannableString valueOf(CharSequence source) {
        return source instanceof SpannableString ? (SpannableString) source : new SpannableString(source);
    }
    public int length() { return mText.length(); }
    public char charAt(int index) { return mText.charAt(index); }
    public CharSequence subSequence(int start, int end) {
        SpanStore.checkRange(start, end, length());
        SpannableString result = new SpannableString(mText.substring(start, end));
        result.mSpans.copy(this, start, end, 0, false);
        return result;
    }
    public void getChars(int start, int end, char[] dest, int destoff) { mText.getChars(start, end, dest, destoff); }
    public String toString() { return mText; }
    public void setSpan(Object what, int start, int end, int flags) { mSpans.set(this, what, start, end, flags); }
    public void removeSpan(Object what) { mSpans.remove(what); }
    public <T> T[] getSpans(int start, int end, Class<T> type) {
        return mSpans.spans(start, end, type);
    }
    public int getSpanStart(Object tag) { return mSpans.start(tag); }
    public int getSpanEnd(Object tag) { return mSpans.end(tag); }
    public int getSpanFlags(Object tag) { return mSpans.flags(tag); }
    public int nextSpanTransition(int start, int limit, Class type) { return mSpans.transition(start, limit, type); }
}
