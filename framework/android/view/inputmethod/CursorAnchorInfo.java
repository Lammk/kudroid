package android.view.inputmethod;

/**
 * Where the cursor and the text around it are on screen, for an IME that wants to
 * position itself relative to them.
 *
 * Built through the Builder, as in AOSP — the class is immutable there and apps use
 * the builder form. Nothing consumes the result under KuDroid (there is no IME), so
 * this only has to hold what was set and give it back.
 */
public final class CursorAnchorInfo {
    public static final int FLAG_HAS_VISIBLE_REGION = 0x01;
    public static final int FLAG_HAS_INVISIBLE_REGION = 0x02;
    public static final int FLAG_IS_RTL = 0x04;

    private final int mSelectionStart;
    private final int mSelectionEnd;
    private final int mComposingTextStart;
    private final CharSequence mComposingText;
    private final float mInsertionMarkerHorizontal;
    private final float mInsertionMarkerTop;
    private final float mInsertionMarkerBaseline;
    private final float mInsertionMarkerBottom;
    private final int mInsertionMarkerFlags;

    private CursorAnchorInfo(Builder b) {
        mSelectionStart = b.mSelectionStart;
        mSelectionEnd = b.mSelectionEnd;
        mComposingTextStart = b.mComposingTextStart;
        mComposingText = b.mComposingText;
        mInsertionMarkerHorizontal = b.mHorizontal;
        mInsertionMarkerTop = b.mTop;
        mInsertionMarkerBaseline = b.mBaseline;
        mInsertionMarkerBottom = b.mBottom;
        mInsertionMarkerFlags = b.mFlags;
    }

    public int getSelectionStart() { return mSelectionStart; }
    public int getSelectionEnd() { return mSelectionEnd; }
    public int getComposingTextStart() { return mComposingTextStart; }
    public CharSequence getComposingText() { return mComposingText; }
    public float getInsertionMarkerHorizontal() { return mInsertionMarkerHorizontal; }
    public float getInsertionMarkerTop() { return mInsertionMarkerTop; }
    public float getInsertionMarkerBaseline() { return mInsertionMarkerBaseline; }
    public float getInsertionMarkerBottom() { return mInsertionMarkerBottom; }
    public int getInsertionMarkerFlags() { return mInsertionMarkerFlags; }

    public static final class Builder {
        int mSelectionStart = -1;
        int mSelectionEnd = -1;
        int mComposingTextStart = -1;
        CharSequence mComposingText;
        float mHorizontal;
        float mTop;
        float mBaseline;
        float mBottom;
        int mFlags;

        public Builder setSelectionRange(int start, int end) {
            mSelectionStart = start;
            mSelectionEnd = end;
            return this;
        }

        public Builder setComposingText(int start, CharSequence text) {
            mComposingTextStart = start;
            mComposingText = text;
            return this;
        }

        public Builder setInsertionMarkerLocation(float horizontal, float top,
                                                  float baseline, float bottom, int flags) {
            mHorizontal = horizontal;
            mTop = top;
            mBaseline = baseline;
            mBottom = bottom;
            mFlags = flags;
            return this;
        }

        public CursorAnchorInfo build() { return new CursorAnchorInfo(this); }

        public void reset() {
            mSelectionStart = -1;
            mSelectionEnd = -1;
            mComposingTextStart = -1;
            mComposingText = null;
            mHorizontal = 0;
            mTop = 0;
            mBaseline = 0;
            mBottom = 0;
            mFlags = 0;
        }
    }
}
