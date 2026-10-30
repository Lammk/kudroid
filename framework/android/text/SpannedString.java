package android.text;

public final class SpannedString implements Spanned, GetChars {
    private final SpannableString mText;
    public SpannedString() { this(""); }
    public SpannedString(CharSequence source) { mText = new SpannableString(source); }
    public static SpannedString valueOf(CharSequence source) {
        return source instanceof SpannedString ? (SpannedString) source : new SpannedString(source);
    }
    public int length() { return mText.length(); }
    public char charAt(int index) { return mText.charAt(index); }
    public String toString() { return mText.toString(); }
    public CharSequence subSequence(int start, int end) { return new SpannedString(mText.subSequence(start, end)); }
    public void getChars(int start, int end, char[] dest, int destoff) { mText.getChars(start, end, dest, destoff); }
    public <T> T[] getSpans(int start, int end, Class<T> type) { return mText.getSpans(start, end, type); }
    public int getSpanStart(Object tag) { return mText.getSpanStart(tag); }
    public int getSpanEnd(Object tag) { return mText.getSpanEnd(tag); }
    public int getSpanFlags(Object tag) { return mText.getSpanFlags(tag); }
    public int nextSpanTransition(int start, int limit, Class type) { return mText.nextSpanTransition(start, limit, type); }
}
