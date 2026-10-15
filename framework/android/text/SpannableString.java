package android.text;

public class SpannableString implements Spannable {
    private final String mText;

    public SpannableString(CharSequence source) {
        mText = source != null ? source.toString() : "";
    }
    public int length() { return mText.length(); }
    public char charAt(int index) { return mText.charAt(index); }
    public CharSequence subSequence(int start, int end) { return mText.subSequence(start, end); }
    public String toString() { return mText; }
    public void setSpan(Object what, int start, int end, int flags) {}
    public void removeSpan(Object what) {}
    public <T> T[] getSpans(int start, int end, Class<T> type) {
        return (T[]) java.lang.reflect.Array.newInstance(type, 0);
    }
    public int getSpanStart(Object tag) { return -1; }
    public int getSpanEnd(Object tag) { return -1; }
    public int getSpanFlags(Object tag) { return 0; }
    public int nextSpanTransition(int start, int limit, Class type) { return limit; }
}
