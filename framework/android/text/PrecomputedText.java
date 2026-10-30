package android.text;

public class PrecomputedText implements Spannable, GetChars {
    private final SpannableString mText;
    private final Params mParams;
    private final int[] mParagraphEnds;

    public PrecomputedText() { this("", new Params.Builder(new TextPaint()).build()); }
    private PrecomputedText(CharSequence text, Params params) {
        if (text == null || params == null) throw new NullPointerException();
        mText = new SpannableString(text, true);
        mParams = params;
        int count = 0;
        for (int i = 0; i < text.length(); i++) if (text.charAt(i) == '\n' || i == text.length() - 1) count++;
        mParagraphEnds = new int[count];
        for (int i = 0, p = 0; i < text.length(); i++) {
            if (text.charAt(i) == '\n' || i == text.length() - 1) mParagraphEnds[p++] = i + 1;
        }
    }
    public static PrecomputedText create(CharSequence text, Params params) {
        return new PrecomputedText(text, params);
    }
    public Params getParams() { return mParams; }
    public int getParagraphCount() { return mParagraphEnds.length; }
    private void checkParagraph(int index) {
        if (index < 0 || index >= mParagraphEnds.length) throw new IllegalArgumentException();
    }
    public int getParagraphStart(int index) { checkParagraph(index); return index == 0 ? 0 : mParagraphEnds[index - 1]; }
    public int getParagraphEnd(int index) { checkParagraph(index); return mParagraphEnds[index]; }
    public int length() { return mText.length(); }
    public char charAt(int index) { return mText.charAt(index); }
    public String toString() { return mText.toString(); }
    public CharSequence subSequence(int start, int end) { return create(mText.subSequence(start, end), mParams); }
    public void getChars(int start, int end, char[] dest, int destoff) { mText.getChars(start, end, dest, destoff); }
    public void setSpan(Object what, int start, int end, int flags) {
        checkMutableSpan(what);
        mText.setSpan(what, start, end, flags);
    }
    public void removeSpan(Object what) { checkMutableSpan(what); mText.removeSpan(what); }
    private static void checkMutableSpan(Object what) {
        if (what instanceof android.text.style.MetricAffectingSpan) {
            throw new IllegalArgumentException("MetricAffectingSpan cannot be changed in PrecomputedText");
        }
    }
    public <T> T[] getSpans(int start, int end, Class<T> type) { return mText.getSpans(start, end, type); }
    public int getSpanStart(Object tag) { return mText.getSpanStart(tag); }
    public int getSpanEnd(Object tag) { return mText.getSpanEnd(tag); }
    public int getSpanFlags(Object tag) { return mText.getSpanFlags(tag); }
    public int nextSpanTransition(int start, int limit, Class type) { return mText.nextSpanTransition(start, limit, type); }

    public static class Params {
        public static class Builder {
            private final TextPaint mPaint;
            private TextDirectionHeuristic mTextDir = TextDirectionHeuristics.FIRSTSTRONG_LTR;
            private int mBreakStrategy = 1;
            private int mHyphenationFrequency = 1;
            public Builder(TextPaint paint) {
                if (paint == null) throw new NullPointerException();
                mPaint = paint;
            }
            public Builder setTextDirection(TextDirectionHeuristic direction) { mTextDir = direction; return this; }
            public Builder setBreakStrategy(int strategy) { mBreakStrategy = strategy; return this; }
            public Builder setHyphenationFrequency(int frequency) { mHyphenationFrequency = frequency; return this; }
            public Params build() { return new Params(mPaint, mTextDir, mBreakStrategy, mHyphenationFrequency); }
        }
        private final TextPaint mPaint;
        private final TextDirectionHeuristic mTextDir;
        private final int mBreakStrategy;
        private final int mHyphenationFrequency;

        public Params(TextPaint paint, TextDirectionHeuristic textDir, int breakStrategy, int hyphenationFrequency) {
            mPaint = paint;
            mTextDir = textDir;
            mBreakStrategy = breakStrategy;
            mHyphenationFrequency = hyphenationFrequency;
        }

        public TextPaint getTextPaint() {
            return mPaint;
        }

        public TextDirectionHeuristic getTextDirection() {
            return mTextDir;
        }

        public int getBreakStrategy() {
            return mBreakStrategy;
        }

        public int getHyphenationFrequency() {
            return mHyphenationFrequency;
        }
    }
}
