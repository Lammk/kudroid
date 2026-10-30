package android.text;

public class Layout {
    private CharSequence mText;
    private TextPaint mPaint;
    private int mWidth;
    private Alignment mAlignment;
    private float mSpacingMult, mSpacingAdd;
    private int[] mStarts = {0, 0};
    private int[] mTops = {0, 0};
    private int[] mBaselines = {0};
    private float[] mWidths = {0};
    private int[] mEllipsisStarts, mEllipsisCounts;
    public Layout() { this("", new TextPaint(), 0, Alignment.ALIGN_NORMAL, 1f, 0f); }
    public Layout(CharSequence text, TextPaint paint, int width, Alignment align, float spacingMult, float spacingAdd) {
        if (width < 0) throw new IllegalArgumentException("negative layout width");
        mText = text;
        mPaint = paint;
        mWidth = width;
        mAlignment = align;
        mSpacingMult = spacingMult;
        mSpacingAdd = spacingAdd;
    }

    // Use the same line data as the measuring and drawing implementation.
    protected final void setLineData(int[] starts, int[] tops, int[] baselines, float[] widths,
            int[] ellipsisStarts, int[] ellipsisCounts) {
        mStarts = starts;
        mTops = tops;
        mBaselines = baselines;
        mWidths = widths;
        mEllipsisStarts = ellipsisStarts;
        mEllipsisCounts = ellipsisCounts;
    }
    public final CharSequence getText() { return mText; }
    public final TextPaint getPaint() { return mPaint; }
    public final int getWidth() { return mWidth; }
    public final Alignment getAlignment() { return mAlignment; }
    public final float getSpacingMultiplier() { return mSpacingMult; }
    public final float getSpacingAdd() { return mSpacingAdd; }
    public int getLineCount() { return mBaselines.length; }
    public int getHeight() { return getLineTop(getLineCount()); }
    public int getLineStart(int line) { return mStarts[line]; }
    public int getLineEnd(int line) { return getLineStart(line + 1); }
    public int getLineTop(int line) { return mTops[line]; }
    public int getLineBottom(int line) { return getLineTop(line + 1); }
    public int getLineBaseline(int line) { return mBaselines[line]; }
    public int getLineDescent(int line) { return getLineBottom(line) - getLineBaseline(line); }
    public float getLineWidth(int line) { return mWidths[line]; }
    public float getLineMax(int line) { return getLineWidth(line); }
    public int getEllipsizedWidth() { return mWidth; }
    public int getEllipsisStart(int line) { return mEllipsisStarts == null ? 0 : mEllipsisStarts[line]; }
    public int getEllipsisCount(int line) { return mEllipsisCounts == null ? 0 : mEllipsisCounts[line]; }
    public int getLineForVertical(int vertical) {
        int low = 0, high = getLineCount() - 1;
        while (low < high) {
            int middle = (low + high + 1) / 2;
            if (getLineTop(middle) <= vertical) low = middle; else high = middle - 1;
        }
        return low;
    }
    public int getLineForOffset(int offset) {
        int low = 0, high = getLineCount() - 1;
        while (low < high) {
            int middle = (low + high + 1) / 2;
            if (getLineStart(middle) <= offset) low = middle; else high = middle - 1;
        }
        return low;
    }

    public enum Alignment {
        ALIGN_NORMAL, ALIGN_OPPOSITE, ALIGN_CENTER, ALIGN_LEFT, ALIGN_RIGHT
    }
}
