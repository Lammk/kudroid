package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.content.res.ColorStateList;
import android.graphics.Paint;
import android.view.View;

/**
 * minimal android.widget.textview implementation.
 *
 * show text. for kudroid minimal framework, text storage and paint.
 */
public class TextView extends View {
    /**
     * Callback when the user presses the action key on the soft keyboard (Done/Search/...).
     */
    public interface OnEditorActionListener {
        boolean onEditorAction(TextView v, int actionId, android.view.KeyEvent event);
    }

    private CharSequence mText = "";
    private int mTextColor = 0xFF000000;
    private android.content.res.ColorStateList mTextColors;
    private float mTextSize = 14.0f;
    private int mGravity = 0;
    private android.text.TextPaint mPaint;
    private int mBreakStrategy = 0;
    private int mHyphenationFrequency = 0;
    private final java.util.List<android.text.TextWatcher> mWatchers =
            new java.util.ArrayList<android.text.TextWatcher>();
    private OnEditorActionListener mOnEditorActionListener;

    // Editing state. mBuffer is the live Editable once the view is edited (or is an
    // EditText); mText then points at it, so every reader sees the same text.
    private EditBuffer mBuffer;
    private int mSelStart;
    private int mSelEnd;
    private CharSequence mHint = "";
    private int mHintColor = 0xFF808080;

    /**
     * The text of an editable view.
     *
     * Every change, whoever makes it (app code, the keyboard, setText), goes through
     * replace/delete/clear here, so TextWatchers fire and the cursor follows.
     */
    private final class EditBuffer extends android.text.SpannableStringBuilder {
        EditBuffer(CharSequence source) {
            super(source);
        }

        @Override
        public android.text.SpannableStringBuilder replace(int st, int en, CharSequence source,
                                                           int start, int end) {
            final String before = toString();
            super.replace(st, en, source, start, end);
            changed(before, st, en);
            return this;
        }

        @Override
        public android.text.SpannableStringBuilder replace(int st, int en, CharSequence text) {
            final String before = toString();
            super.replace(st, en, text);
            changed(before, st, en);
            return this;
        }

        @Override
        public android.text.SpannableStringBuilder delete(int st, int en) {
            final String before = toString();
            super.delete(st, en);
            changed(before, st, en);
            return this;
        }

        @Override
        public void clear() {
            final String before = toString();
            super.clear();
            changed(before, 0, before.length());
        }

        /** [st, en) of |before| was replaced; report it to the watchers. */
        private void changed(String before, int st, int en) {
            final int removed = en - st;
            final int added = length() - (before.length() - removed);
            mSelStart = adjustOffset(mSelStart, st, en, added);
            mSelEnd = adjustOffset(mSelEnd, st, en, added);

            final android.text.TextWatcher[] watchers =
                    mWatchers.toArray(new android.text.TextWatcher[0]);
            for (int i = watchers.length - 1; i >= 0; --i) {
                watchers[i].beforeTextChanged(before, st, removed, added);
            }
            for (int i = watchers.length - 1; i >= 0; --i) {
                watchers[i].onTextChanged(this, st, removed, added);
            }
            for (int i = watchers.length - 1; i >= 0; --i) {
                watchers[i].afterTextChanged(this);
            }
            textChanged();
        }
    }

    /** Where an offset lands after [st, en) is replaced by |added| characters. */
    private static int adjustOffset(int p, int st, int en, int added) {
        if (p < st) return p;
        if (p <= en) return st + added;
        return p + added - (en - st);
    }

    /** True for views whose text the user changes (EditText). */
    protected boolean isEditable() {
        return false;
    }

    private EditBuffer buffer() {
        if (mBuffer == null) {
            mBuffer = new EditBuffer(mText);
            mBuffer.setFilters(mFilters);
            mText = mBuffer;
        }
        return mBuffer;
    }

    /** The live Editable behind this view's text. */
    protected final android.text.Editable getEditableBuffer() {
        return buffer();
    }

    private int clampOffset(int p) {
        final int len = mText != null ? mText.length() : 0;
        return p < 0 ? 0 : (p > len ? len : p);
    }

    /** Move the cursor / selection; offsets are clamped to the text. */
    protected final void setSelectionRange(int start, int stop) {
        mSelStart = clampOffset(start);
        mSelEnd = clampOffset(stop);
        refresh();
    }

    public int getSelectionStart() {
        return mBuffer != null || isEditable() ? clampOffset(mSelStart) : -1;
    }

    public int getSelectionEnd() {
        return mBuffer != null || isEditable() ? clampOffset(mSelEnd) : -1;
    }

    public boolean hasSelection() {
        return getSelectionStart() != getSelectionEnd();
    }

    /** Ask the Activity to repaint the software UI after a visible change. */
    private void refresh() {
        invalidate();
        final android.content.Context c = getContext();
        if (c instanceof android.app.Activity) {
            ((android.app.Activity) c).requestRender();
        }
    }

    /** The text changed: the size may have too. */
    private void textChanged() {
        requestLayout();
        refresh();
    }

    public TextView(Context context) {
        this(context, null);
    }

    public TextView(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public TextView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public TextView(Context context, android.util.AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        mPaint = new android.text.TextPaint();
        mPaint.setColor(mTextColor);
        mPaint.setTextSize(mTextSize);
        readAttributes(context, attrs, defStyleAttr, defStyleRes);
    }

    /** Apply the attributes the view draws with: appearance first, explicit values over it. */
    private void readAttributes(Context context, android.util.AttributeSet attrs,
                                int defStyleAttr, int defStyleRes) {
        final android.content.res.TypedArray a = context.obtainStyledAttributes(
                attrs, com.android.internal.R.styleable.TextView, defStyleAttr, defStyleRes);
        try {
            final int appearance = a.getResourceId(
                    com.android.internal.R.styleable.TextView_textAppearance, 0);
            if (appearance != 0) setTextAppearance(context, appearance);
            if (a.hasValue(com.android.internal.R.styleable.TextView_textSize)) {
                setRawTextSize(a.getDimension(
                        com.android.internal.R.styleable.TextView_textSize, mTextSize));
            }
            final android.content.res.ColorStateList colors = a.getColorStateList(
                    com.android.internal.R.styleable.TextView_textColor);
            if (colors != null) setTextColor(colors);
            final android.content.res.ColorStateList hintColors = a.getColorStateList(
                    com.android.internal.R.styleable.TextView_textColorHint);
            if (hintColors != null) mHintColor = hintColors.getDefaultColor();
            mGravity = a.getInt(com.android.internal.R.styleable.TextView_gravity, mGravity);
            final CharSequence text = a.getText(com.android.internal.R.styleable.TextView_text);
            if (text != null) mText = text;
            final CharSequence hint = a.getText(com.android.internal.R.styleable.TextView_hint);
            if (hint != null) mHint = hint;
            mAllCaps = a.getBoolean(com.android.internal.R.styleable.TextView_textAllCaps, mAllCaps);
            mSingleLine = a.getBoolean(com.android.internal.R.styleable.TextView_singleLine, mSingleLine);
            mMaxLines = a.getInt(com.android.internal.R.styleable.TextView_maxLines, mMaxLines);
            mMinLines = a.getInt(com.android.internal.R.styleable.TextView_minLines, mMinLines);
            mLines = a.getInt(com.android.internal.R.styleable.TextView_lines, mLines);
            mMinWidth = a.getDimensionPixelSize(com.android.internal.R.styleable.TextView_minWidth, mMinWidth);
            mMaxWidth = a.getDimensionPixelSize(com.android.internal.R.styleable.TextView_maxWidth, mMaxWidth);
            mMinHeight = a.getDimensionPixelSize(com.android.internal.R.styleable.TextView_minHeight, mMinHeight);
            mMaxHeight = a.getDimensionPixelSize(com.android.internal.R.styleable.TextView_maxHeight, mMaxHeight);
            mMinHeightInLines = !a.hasValue(com.android.internal.R.styleable.TextView_minHeight);
            mMaxHeightInLines = !a.hasValue(com.android.internal.R.styleable.TextView_maxHeight);
            if (a.hasValue(com.android.internal.R.styleable.TextView_minLines)) mMinHeightInLines = true;
            if (a.hasValue(com.android.internal.R.styleable.TextView_maxLines)) mMaxHeightInLines = true;
            if (mLines >= 0) {
                mMinLines = mMaxLines = mLines;
                mMinHeightInLines = mMaxHeightInLines = true;
            }
            if (mSingleLine) {
                mMinLines = mMaxLines = 1;
                mMinHeightInLines = mMaxHeightInLines = true;
                mHorizontallyScrolling = true;
            }
            mSpacingAdd = a.getDimensionPixelSize(com.android.internal.R.styleable.TextView_lineSpacingExtra, (int) mSpacingAdd);
            mSpacingMult = a.getFloat(com.android.internal.R.styleable.TextView_lineSpacingMultiplier, mSpacingMult);
            mHorizontallyScrolling = a.getBoolean(
                    com.android.internal.R.styleable.TextView_scrollHorizontally, mHorizontallyScrolling);
            mDrawablePadding = a.getDimensionPixelSize(
                    com.android.internal.R.styleable.TextView_drawablePadding, mDrawablePadding);
            final int ellipsize = a.getInt(com.android.internal.R.styleable.TextView_ellipsize, 0);
            if (ellipsize > 0) {
                mEllipsize = ELLIPSIZE_END;
                mTruncateAt = android.text.TextUtils.TruncateAt.END;
            }
            final android.graphics.drawable.Drawable left = a.getDrawable(
                    com.android.internal.R.styleable.TextView_drawableLeft);
            final android.graphics.drawable.Drawable start = a.getDrawable(
                    com.android.internal.R.styleable.TextView_drawableStart);
            final android.graphics.drawable.Drawable top = a.getDrawable(
                    com.android.internal.R.styleable.TextView_drawableTop);
            final android.graphics.drawable.Drawable right = a.getDrawable(
                    com.android.internal.R.styleable.TextView_drawableRight);
            final android.graphics.drawable.Drawable end = a.getDrawable(
                    com.android.internal.R.styleable.TextView_drawableEnd);
            final android.graphics.drawable.Drawable bottom = a.getDrawable(
                    com.android.internal.R.styleable.TextView_drawableBottom);
            if (left != null || start != null || top != null || right != null || end != null
                    || bottom != null) {
                setCompoundDrawablesWithIntrinsicBounds(start != null ? start : left, top,
                        end != null ? end : right, bottom);
            }
        } finally {
            a.recycle();
        }
    }

    public void addTextChangedListener(android.text.TextWatcher watcher) {
        if (watcher != null && !mWatchers.contains(watcher)) mWatchers.add(watcher);
    }

    public void removeTextChangedListener(android.text.TextWatcher watcher) {
        mWatchers.remove(watcher);
    }

    public void setOnEditorActionListener(OnEditorActionListener l) {
        mOnEditorActionListener = l;
    }

    /**
     * The user pressed the keyboard's action key (Done/Search/...).
     *
     * The app's listener gets first refusal. Without one, Done dismisses the keyboard,
     * as it does on a device.
     */
    public void onEditorAction(int actionCode) {
        final OnEditorActionListener listener = mOnEditorActionListener;
        if (listener != null && listener.onEditorAction(this, actionCode, null)) return;
        if (actionCode == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
            android.view.inputmethod.InputMethodManager.getInstance()
                    .hideSoftInputFromWindow(getWindowToken(), 0);
        }
    }

    /**
     * returns text.
     */
    public CharSequence getText() {
        return mText;
    }

    /**
     * set text.
     */
    public void setText(CharSequence text) {
        final CharSequence next = text != null ? text : "";
        if (isEditable()) {
            // Through the buffer, so watchers fire (afterTextChanged included) and the
            // cursor ends up after the new text.
            final EditBuffer b = buffer();
            b.replace(0, b.length(), next);
            return;
        }
        final CharSequence old = mText;
        final int oldLen = old.length();
        for (int i = mWatchers.size() - 1; i >= 0; --i) {
            mWatchers.get(i).beforeTextChanged(old, 0, oldLen, next.length());
        }
        mBuffer = null;
        mText = next;
        for (int i = mWatchers.size() - 1; i >= 0; --i) {
            mWatchers.get(i).onTextChanged(mText, 0, oldLen, mText.length());
        }
        textChanged();
    }

    /**
     * set text from a resource id.
     */
    public void setText(int resId) {
        setText(getContext().getResources().getText(resId));
    }

    public enum BufferType { NORMAL, SPANNABLE, EDITABLE }

    public void setText(CharSequence text, BufferType type) {
        setText(text);
    }

    public void setText(int resId, BufferType type) {
        setText(resId);
    }

    public void setText(char[] text, int start, int len) {
        setText(new String(text, start, len));
    }

    public android.text.TextPaint getPaint() {
        if (mPaint == null) {
            mPaint = new android.text.TextPaint();
            mPaint.setColor(mTextColor);
            mPaint.setTextSize(mTextSize);
        }
        return mPaint;
    }

    public android.text.PrecomputedText.Params getTextMetricsParams() {
        return new android.text.PrecomputedText.Params(
                new android.text.TextPaint(getPaint()),
                getTextDirectionHeuristic(),
                getBreakStrategy(),
                getHyphenationFrequency());
    }

    public void setTextMetricsParams(android.text.PrecomputedText.Params params) {
        if (params != null && params.getTextPaint() != null) {
            getPaint().set(params.getTextPaint());
            mBreakStrategy = params.getBreakStrategy();
            mHyphenationFrequency = params.getHyphenationFrequency();
        }
    }

    public int getBreakStrategy() {
        return mBreakStrategy;
    }

    public void setBreakStrategy(int breakStrategy) {
        mBreakStrategy = breakStrategy;
    }

    public int getHyphenationFrequency() {
        return mHyphenationFrequency;
    }

    public void setHyphenationFrequency(int hyphenationFrequency) {
        mHyphenationFrequency = hyphenationFrequency;
    }

    public android.text.TextDirectionHeuristic getTextDirectionHeuristic() {
        return android.text.TextDirectionHeuristics.FIRSTSTRONG_LTR;
    }

    public final void append(CharSequence text) {
        if (text != null) append(text, 0, text.length());
    }

    /** Add text at the end; a plain TextView becomes editable-backed, as on Android. */
    public void append(CharSequence text, int start, int end) {
        if (text == null) return;
        buffer().append(text, start, end);
    }

    // Input filters.
    private android.text.InputFilter[] mFilters = new android.text.InputFilter[0];

    public void setFilters(android.text.InputFilter[] filters) {
        mFilters = filters != null ? filters : new android.text.InputFilter[0];
        if (mBuffer != null) mBuffer.setFilters(mFilters);
    }

    public android.text.InputFilter[] getFilters() {
        return mFilters;
    }

    /**
     * Run `source` through the filter chain.
     *
     * Each filter may replace the text or leave it alone (null). Later filters see the
     * output of earlier ones, which is what makes a length cap after an upper-caser
     * behave the way the app intends.
     */
    protected CharSequence applyFilters(CharSequence source, android.text.Spanned dest,
                                        int dstart, int dend) {
        CharSequence out = source != null ? source : "";
        for (int i = 0; i < mFilters.length; ++i) {
            if (mFilters[i] == null) continue;
            CharSequence replacement =
                    mFilters[i].filter(out, 0, out.length(), dest, dstart, dend);
            if (replacement != null) out = replacement;
        }
        return out;
    }

    /**
     * set text color.
     */
    public void setTextColor(int color) {
        mTextColors = null;
        mTextColor = color;
        mPaint.setColor(color);
        invalidate();
    }

    /**
     * set text size.
     */
    public void setTextSize(float size) {
        setRawTextSize(size);
    }

    /** Size in the given TypedValue unit (COMPLEX_UNIT_SP, _DIP, _PX, ...). */
    public void setTextSize(int unit, float size) {
        final android.content.res.Resources r = getContext() != null
                ? getContext().getResources() : android.content.res.Resources.getSystem();
        setRawTextSize(android.util.TypedValue.applyDimension(unit, size, r.getDisplayMetrics()));
    }

    private void setRawTextSize(float size) {
        if (mTextSize == size) return;
        mTextSize = size;
        mPaint.setTextSize(size);
        requestLayout();
        invalidate();
    }

    public void setTextColor(android.content.res.ColorStateList colors) {
        if (colors == null) throw new NullPointerException();
        setTextColor(colors.getColorForState(getDrawableState(), colors.getDefaultColor()));
        mTextColors = colors;
    }

    public ColorStateList getHintTextColors() {
        return ColorStateList.valueOf(mHintColor);
    }

    public android.content.res.ColorStateList getTextColors() {
        return mTextColors != null ? mTextColors
                : android.content.res.ColorStateList.valueOf(mTextColor);
    }

    public void setTextAppearance(int resId) {
        setTextAppearance(getContext(), resId);
    }

    /** Apply textSize/textColor/textColorHint of a TextAppearance style. */
    public void setTextAppearance(Context context, int resId) {
        final android.content.res.TypedArray a = context.obtainStyledAttributes(
                resId, com.android.internal.R.styleable.TextAppearance);
        try {
            if (a.hasValue(com.android.internal.R.styleable.TextAppearance_textSize)) {
                setRawTextSize(a.getDimension(
                        com.android.internal.R.styleable.TextAppearance_textSize, mTextSize));
            }
            final android.content.res.ColorStateList colors = a.getColorStateList(
                    com.android.internal.R.styleable.TextAppearance_textColor);
            if (colors != null) setTextColor(colors);
            final android.content.res.ColorStateList hint = a.getColorStateList(
                    com.android.internal.R.styleable.TextAppearance_textColorHint);
            if (hint != null) mHintColor = hint.getDefaultColor();
        } finally {
            a.recycle();
        }
    }

    /**
     * set gravity.
     */
    public void setGravity(int gravity) {
        mGravity = gravity;
        invalidate();
    }

    /**
     * returns gravity.
     */
    public int getGravity() {
        return mGravity;
    }

    /**
     * returns text color.
     */
    public int getCurrentTextColor() {
        return mTextColor;
    }

    /**
     * returns the text size.
     */
    public float getTextSize() {
        return mTextSize;
    }

    // ---- Text layout: paragraphs split on '\n', greedy word wrap to the content width.

    /** Font box as a multiple of the text size, split at the baseline. */
    private static final float FONT_ASCENT = 0.93f;
    private static final float FONT_DESCENT = 0.27f;

    /** Narrowest an editable field measures, in text sizes. */
    private static final float MIN_EDIT_WIDTH_EM = 8f;

    private static final int ELLIPSIZE_END = 3;

    private int mMinWidth = 0;
    private int mMaxWidth = Integer.MAX_VALUE;
    private int mMinHeight = 0;
    private int mMaxHeight = Integer.MAX_VALUE;
    private int mEllipsize = -1;
    private int mDrawablePadding = 0;
    private android.content.res.ColorStateList mCompoundDrawableTintList;
    private android.graphics.PorterDuff.Mode mCompoundDrawableTintMode =
            android.graphics.PorterDuff.Mode.SRC_IN;
    private final android.graphics.drawable.Drawable[] mDrawables =
            new android.graphics.drawable.Drawable[4];

    // Cached line breaks for (text, hint-ness, width, size).
    private String mLaidText;
    private int mLaidWidth = -2;
    private float mLaidSize;
    private int mLaidMaxLines, mLaidEllipsize;
    private boolean mLaidWrap;
    private float mLaidSpacingMult, mLaidSpacingAdd;
    private android.text.Layout mLayout;
    private int[] mLineStart = new int[0];
    private int[] mLineEnd = new int[0];
    private String[] mLineText = new String[0];

    private float lineHeight() {
        return (mTextSize * (FONT_ASCENT + FONT_DESCENT)) * mSpacingMult + mSpacingAdd;
    }

    private boolean isShowingHint() {
        return (mText == null || mText.length() == 0) && mHint != null && mHint.length() > 0;
    }

    /** The string that is laid out: the text, or the hint while the text is empty. */
    private String layoutSource() {
        return isShowingHint() ? mHint.toString() : displayText();
    }

    private boolean wrapsLines() {
        return !mSingleLine && !mHorizontallyScrolling;
    }

    private int maxLineCount() {
        return mMaxHeightInLines ? Math.max(0, mMaxLines) : Integer.MAX_VALUE;
    }

    private int minLineCount() {
        return mMinHeightInLines ? Math.max(0, mMinLines) : 0;
    }

    /** Break the source into lines no wider than |width| (<= 0 means unlimited). */
    private void layoutLines(int width) {
        final String source = layoutSource();
        if (source.equals(mLaidText) && width == mLaidWidth && mTextSize == mLaidSize
                && maxLineCount() == mLaidMaxLines && wrapsLines() == mLaidWrap
                && mEllipsize == mLaidEllipsize && mSpacingMult == mLaidSpacingMult
                && mSpacingAdd == mLaidSpacingAdd) {
            return;
        }
        final java.util.ArrayList<int[]> spans = new java.util.ArrayList<int[]>();
        final boolean wrap = wrapsLines() && width > 0;
        int paraStart = 0;
        for (int i = 0; i <= source.length(); i++) {
            if (i != source.length() && source.charAt(i) != '\n') continue;
            if (!wrap) {
                spans.add(new int[] {paraStart, i});
            } else {
                wrapParagraph(source, paraStart, i, width, spans);
            }
            paraStart = i + 1;
        }
        final int max = maxLineCount();
        final boolean truncated = spans.size() > max;
        while (spans.size() > max) spans.remove(spans.size() - 1);
        final int n = spans.size();
        mLineStart = new int[n];
        mLineEnd = new int[n];
        mLineText = new String[n];
        final int[] starts = new int[n + 1], tops = new int[n + 1], baselines = new int[n];
        final float[] widths = new float[n];
        final int[] ellipsisStarts = mEllipsize > 0 ? new int[n + 1] : null;
        final int[] ellipsisCounts = mEllipsize > 0 ? new int[n + 1] : null;
        for (int i = 0; i < n; i++) {
            final int[] span = spans.get(i);
            mLineStart[i] = span[0];
            mLineEnd[i] = span[1];
            String line = source.substring(span[0], span[1]);
            final boolean last = i == n - 1;
            final boolean cut = last && truncated;
            if (mEllipsize > 0 && width > 0 && (cut || (!wrap && mPaint.measureText(line) > width))) {
                line = ellipsizeLine(line, width, cut);
                ellipsisStarts[i] = line.length() - 1;
                ellipsisCounts[i] = (cut ? source.length() : span[1])
                        - span[0] - ellipsisStarts[i];
            }
            mLineText[i] = line;
            starts[i] = span[0];
            tops[i] = Math.round(i * lineHeight());
            baselines[i] = Math.round(i * lineHeight() + mTextSize * FONT_ASCENT);
            widths[i] = mPaint.measureText(line);
        }
        starts[n] = source.length();
        tops[n] = Math.round(n * lineHeight());
        mLayout = new TextViewLayout(source, mPaint, Math.max(0, width), mSpacingMult,
                mSpacingAdd, starts, tops, baselines, widths, ellipsisStarts, ellipsisCounts);
        mLaidText = source;
        mLaidWidth = width;
        mLaidSize = mTextSize;
        mLaidMaxLines = maxLineCount();
        mLaidWrap = wrapsLines();
        mLaidEllipsize = mEllipsize;
        mLaidSpacingMult = mSpacingMult;
        mLaidSpacingAdd = mSpacingAdd;
    }

    private static final class TextViewLayout extends android.text.Layout {
        TextViewLayout(CharSequence text, android.text.TextPaint paint, int width, float mult,
                float add, int[] starts, int[] tops, int[] baselines, float[] widths,
                int[] ellipsisStarts, int[] ellipsisCounts) {
            super(text, paint, width, Alignment.ALIGN_NORMAL, mult, add);
            setLineData(starts, tops, baselines, widths, ellipsisStarts, ellipsisCounts);
        }
    }

    public final android.text.Layout getLayout() { return mLayout; }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        layoutLines(Math.max(0, w - getCompoundPaddingLeft() - getCompoundPaddingRight()));
    }

    private void wrapParagraph(String source, int start, int end, int width,
                               java.util.ArrayList<int[]> out) {
        if (start == end) {
            out.add(new int[] {start, end});
            return;
        }
        final float space = mPaint.measureText(" ");
        int lineStart = start;
        float lineWidth = 0f;
        int i = start;
        while (i < end) {
            int wordEnd = i;
            while (wordEnd < end && source.charAt(wordEnd) != ' ') wordEnd++;
            final float wordWidth = mPaint.measureText(source.substring(i, wordEnd));
            final boolean first = i == lineStart;
            if (!first && lineWidth + space + wordWidth > width) {
                out.add(new int[] {lineStart, i - 1});
                lineStart = i;
                lineWidth = 0f;
            }
            if (wordWidth > width && lineWidth == 0f) {
                // A word wider than the line is broken between characters.
                int c = i;
                float w = 0f;
                while (c < wordEnd) {
                    final float cw = mPaint.measureText(source.substring(c, c + 1));
                    if (w + cw > width && c > lineStart) {
                        out.add(new int[] {lineStart, c});
                        lineStart = c;
                        w = 0f;
                    }
                    w += cw;
                    c++;
                }
                lineWidth = w;
            } else {
                lineWidth = (lineWidth == 0f ? 0f : lineWidth + space) + wordWidth;
            }
            i = wordEnd + 1;
        }
        out.add(new int[] {lineStart, end});
    }

    /** Trim |line| so it plus an ellipsis fits |width|; |force| adds one even if it fits. */
    private String ellipsizeLine(String line, int width, boolean force) {
        final String dots = "\u2026";
        if (!force && mPaint.measureText(line) <= width) return line;
        final float dotsWidth = mPaint.measureText(dots);
        int n = line.length();
        while (n > 0 && mPaint.measureText(line.substring(0, n)) + dotsWidth > width) n--;
        return line.substring(0, n).trim() + dots;
    }

    private int compoundWidth(int index) {
        final android.graphics.drawable.Drawable d = mDrawables[index];
        return d != null ? d.getBounds().width() : 0;
    }

    private int compoundHeight(int index) {
        final android.graphics.drawable.Drawable d = mDrawables[index];
        return d != null ? d.getBounds().height() : 0;
    }

    public int getCompoundPaddingLeft() {
        return getPaddingLeft() + (mDrawables[0] != null ? compoundWidth(0) + mDrawablePadding : 0);
    }

    public int getCompoundPaddingRight() {
        return getPaddingRight() + (mDrawables[2] != null ? compoundWidth(2) + mDrawablePadding : 0);
    }

    public int getCompoundPaddingTop() {
        return getPaddingTop() + (mDrawables[1] != null ? compoundHeight(1) + mDrawablePadding : 0);
    }

    public int getCompoundPaddingBottom() {
        return getPaddingBottom() + (mDrawables[3] != null ? compoundHeight(3) + mDrawablePadding : 0);
    }

    public int getCompoundPaddingStart() { return getCompoundPaddingLeft(); }
    public int getCompoundPaddingEnd() { return getCompoundPaddingRight(); }
    public int getTotalPaddingLeft() { return getCompoundPaddingLeft(); }
    public int getTotalPaddingRight() { return getCompoundPaddingRight(); }
    public int getTotalPaddingTop() { return getCompoundPaddingTop(); }
    public int getTotalPaddingBottom() { return getCompoundPaddingBottom(); }
    public int getTotalPaddingStart() { return getCompoundPaddingLeft(); }
    public int getTotalPaddingEnd() { return getCompoundPaddingRight(); }

    /**
     * Width and height from the laid-out text plus padding, honouring min/max and the parent's
     * spec.
     */
    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        final int widthMode = MeasureSpec.getMode(widthMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        final int widthSize = MeasureSpec.getSize(widthMeasureSpec);
        final int heightSize = MeasureSpec.getSize(heightMeasureSpec);
        final int padH = getCompoundPaddingLeft() + getCompoundPaddingRight();
        final int padV = getCompoundPaddingTop() + getCompoundPaddingBottom();

        // Widest unwrapped paragraph.
        final String source = layoutSource();
        float desired = 0f;
        int paraStart = 0;
        for (int i = 0; i <= source.length(); i++) {
            if (i != source.length() && source.charAt(i) != '\n') continue;
            if (i > paraStart) desired = Math.max(desired, mPaint.measureText(source.substring(paraStart, i)));
            paraStart = i + 1;
        }
        if (isEditable()) {
            desired = Math.max(desired, mTextSize * MIN_EDIT_WIDTH_EM);
        }

        int width;
        if (widthMode == MeasureSpec.EXACTLY) {
            width = widthSize;
        } else {
            width = (int) Math.ceil(desired) + padH;
            width = Math.min(width, mMaxWidth);
            width = Math.max(width, mMinWidth);
            width = Math.max(width, getSuggestedMinimumWidth());
            if (widthMode == MeasureSpec.AT_MOST) width = Math.min(widthSize, width);
        }

        layoutLines(Math.max(0, width - padH));

        int height;
        if (heightMode == MeasureSpec.EXACTLY) {
            height = heightSize;
        } else {
            final int lines = Math.max(Math.max(mLineText.length, 1), minLineCount());
            height = (int) Math.ceil(lineHeight() * lines) + padV;
            if (!mMaxHeightInLines) height = Math.min(height, mMaxHeight);
            if (!mMinHeightInLines) height = Math.max(height, mMinHeight);
            height = Math.max(height, getSuggestedMinimumHeight());
            if (heightMode == MeasureSpec.AT_MOST) height = Math.min(heightSize, height);
        }
        setMeasuredDimension(width, height);
    }

    @Override
    public int getBaseline() {
        final int avail = Math.max(0, getHeight() - getCompoundPaddingTop() - getCompoundPaddingBottom());
        final float block = lineHeight() * Math.max(1, mLineText.length);
        return getCompoundPaddingTop() + (int) verticalOffset(avail, block) + (int) (mTextSize * FONT_ASCENT);
    }

    private float verticalOffset(int avail, float block) {
        switch (mGravity & android.view.Gravity.VERTICAL_GRAVITY_MASK) {
            case android.view.Gravity.BOTTOM: return Math.max(0f, avail - block);
            case android.view.Gravity.CENTER_VERTICAL: return Math.max(0f, (avail - block) / 2f);
            default: return 0f;
        }
    }

    private float horizontalOffset(int avail, float lineWidth) {
        final int h = mGravity & android.view.Gravity.RELATIVE_HORIZONTAL_GRAVITY_MASK;
        if (h == android.view.Gravity.CENTER_HORIZONTAL) return (avail - lineWidth) / 2f;
        if (h == android.view.Gravity.RIGHT || h == android.view.Gravity.END) return avail - lineWidth;
        return 0f;
    }

    /** Re-evaluate the state-dependent text colour. */
    @Override
    protected void drawableStateChanged() {
        super.drawableStateChanged();
        if (mTextColors != null) {
            final int color = mTextColors.getColorForState(getDrawableState(), mTextColors.getDefaultColor());
            if (color != mTextColor) {
                mTextColor = color;
                mPaint.setColor(color);
                invalidate();
            }
        }
        // Runs from the View constructor, before this class's fields exist.
        if (mDrawables == null) return;
        for (int i = 0; i < 4; i++) {
            final android.graphics.drawable.Drawable d = mDrawables[i];
            if (d != null && d.isStateful()) d.setState(getDrawableState());
        }
    }

    /** True when the keyboard must not echo the text (password fields). */
    private boolean isPasswordInput() {
        if (mTransformation instanceof android.text.method.PasswordTransformationMethod) {
            return true;
        }
        final int cls = mInputType & android.text.InputType.TYPE_MASK_CLASS;
        final int variation = mInputType & android.text.InputType.TYPE_MASK_VARIATION;
        if (cls == android.text.InputType.TYPE_CLASS_TEXT) {
            return variation == android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
                    || variation == android.text.InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
                    || variation == android.text.InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD;
        }
        return cls == android.text.InputType.TYPE_CLASS_NUMBER
                && variation == android.text.InputType.TYPE_NUMBER_VARIATION_PASSWORD;
    }

    /** What is drawn and measured: the text, with a password's characters masked. */
    private String displayText() {
        String text = mText != null ? mText.toString() : "";
        if (mAllCaps) text = text.toUpperCase();
        if (!isPasswordInput()) return text;
        final StringBuilder dots = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) dots.append('•');
        return dots.toString();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (canvas == null) return;

        final int width = getWidth();
        final int height = getHeight();
        final int cpl = getCompoundPaddingLeft();
        final int cpr = getCompoundPaddingRight();
        final int cpt = getCompoundPaddingTop();
        final int cpb = getCompoundPaddingBottom();
        final int availW = Math.max(0, width - cpl - cpr);
        final int availH = Math.max(0, height - cpt - cpb);

        drawCompoundDrawables(canvas, width, height, cpt, cpb);

        layoutLines(availW);
        final int lines = mLineText.length;
        final boolean hint = isShowingHint();
        final float lineHeight = lineHeight();
        final float top = cpt + verticalOffset(availH, lineHeight * lines);

        final Paint paint;
        if (hint) {
            paint = new Paint();
            paint.setColor(mHintColor);
            paint.setTextSize(mTextSize);
        } else {
            paint = mPaint;
        }

        final int save = canvas.save();
        canvas.clipRect(cpl, cpt, cpl + availW, cpt + availH);
        // A single-line editor keeps the caret in view by shifting the text left.
        float scrollShift = 0f;
        final boolean editing = isEditable() && isFocused() && mCursorVisible;
        final String text = layoutSource();
        if (editing && !wrapsLines()) {
            final int cursor = Math.min(clampOffset(mSelStart), text.length());
            final float caretX = mPaint.measureText(text.substring(0, cursor));
            if (caretX > availW - 2) scrollShift = availW - 2 - caretX;
        }
        for (int i = 0; i < lines; i++) {
            final String line = mLineText[i];
            if (line.length() == 0) continue;
            final float x = cpl + horizontalOffset(availW, paint.measureText(line)) + scrollShift;
            final float baseline = top + i * lineHeight + mTextSize * FONT_ASCENT;
            canvas.drawText(line, x, baseline, paint);
        }

        if (editing) {
            final int cursor = Math.min(clampOffset(mSelStart), text.length());
            int line = 0;
            for (int i = 0; i < lines; i++) {
                if (cursor >= mLineStart[i]) line = i;
            }
            final int from = lines > 0 ? mLineStart[line] : 0;
            final String before = text.substring(Math.min(from, cursor), cursor);
            final float lineWidth = lines > 0 ? mPaint.measureText(mLineText[line]) : 0f;
            final float x = cpl + horizontalOffset(availW, lineWidth) + scrollShift
                    + mPaint.measureText(before);
            final float caretTop = top + line * lineHeight;
            canvas.drawRect(x, caretTop, x + 2, caretTop + lineHeight, mPaint);
        }
        canvas.restoreToCount(save);
    }

    private void drawCompoundDrawables(Canvas canvas, int width, int height, int cpt, int cpb) {
        final int pl = getPaddingLeft();
        final int pr = getPaddingRight();
        final int pt = getPaddingTop();
        final int pb = getPaddingBottom();
        final int vspace = height - cpb - cpt;
        final int hspace = width - getCompoundPaddingRight() - getCompoundPaddingLeft();
        final android.graphics.drawable.Drawable left = mDrawables[0];
        final android.graphics.drawable.Drawable topD = mDrawables[1];
        final android.graphics.drawable.Drawable right = mDrawables[2];
        final android.graphics.drawable.Drawable bottom = mDrawables[3];
        if (left != null) {
            final android.graphics.Rect b = left.getBounds();
            final int y = cpt + (vspace - b.height()) / 2;
            drawDrawableAt(canvas, left, pl, y);
        }
        if (right != null) {
            final android.graphics.Rect b = right.getBounds();
            final int y = cpt + (vspace - b.height()) / 2;
            drawDrawableAt(canvas, right, width - pr - b.width(), y);
        }
        if (topD != null) {
            final android.graphics.Rect b = topD.getBounds();
            drawDrawableAt(canvas, topD, getCompoundPaddingLeft() + (hspace - b.width()) / 2, pt);
        }
        if (bottom != null) {
            final android.graphics.Rect b = bottom.getBounds();
            drawDrawableAt(canvas, bottom, getCompoundPaddingLeft() + (hspace - b.width()) / 2,
                    height - pb - b.height());
        }
    }

    private static void drawDrawableAt(Canvas canvas, android.graphics.drawable.Drawable d, int x, int y) {
        final android.graphics.Rect b = d.getBounds();
        final int w = b.width();
        final int h = b.height();
        canvas.save();
        canvas.translate(x, y);
        d.setBounds(0, 0, w, h);
        d.draw(canvas);
        d.setBounds(b);
        canvas.restore();
    }

    private android.text.method.TransformationMethod mTransformation;
    private android.text.method.MovementMethod mMovement;
    private android.text.method.KeyListener mKeyListener;
    private android.text.Editable.Factory mEditableFactory = android.text.Editable.Factory.getInstance();
    private int mInputType = 0;
    private int mImeOptions = 0;
    private String mPrivateImeOptions = null;
    private CharSequence mImeActionLabel = null;
    private int mImeActionId = 0;
    private boolean mSingleLine = false;
    private int mLines = -1;
    private int mMinLines = 0;
    private int mMaxLines = Integer.MAX_VALUE;
    private boolean mMinHeightInLines = true;
    private boolean mMaxHeightInLines = true;
    private boolean mHorizontallyScrolling = false;
    private boolean mSelectAllOnFocus = false;
    private boolean mCursorVisible = true;
    private boolean mTextIsSelectable = false;
    private int mHighlightColor = 0x6633B5E5;
    private boolean mIncludeFontPadding = true;
    private boolean mAllCaps = false;
    private float mSpacingMult = 1.0f;
    private float mSpacingAdd = 0.0f;

    public void setTransformationMethod(android.text.method.TransformationMethod method) {
        mTransformation = method;
    }

    public android.text.method.TransformationMethod getTransformationMethod() {
        return mTransformation;
    }

    public void setMovementMethod(android.text.method.MovementMethod movement) {
        mMovement = movement;
    }

    public android.text.method.MovementMethod getMovementMethod() {
        return mMovement;
    }

    public void setKeyListener(android.text.method.KeyListener input) {
        mKeyListener = input;
    }

    public android.text.method.KeyListener getKeyListener() {
        return mKeyListener;
    }

    public void setEditableFactory(android.text.Editable.Factory factory) {
        mEditableFactory = factory != null ? factory : android.text.Editable.Factory.getInstance();
    }

    public int getInputType() {
        return mInputType;
    }

    public void setInputType(int type) {
        mInputType = type;
        // A text field is single-line unless its type says multi-line (AOSP rule).
        if (type != 0) mSingleLine = !isMultilineInputType(type);
    }

    private static boolean isMultilineInputType(int type) {
        return (type & (android.text.InputType.TYPE_MASK_CLASS
                | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE))
                == (android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE);
    }

    /** True when Return should insert a line break rather than act as an action. */
    boolean isMultiLineInput() {
        return !mSingleLine;
    }

    public void setHint(CharSequence hint) {
        mHint = hint != null ? hint : "";
        refresh();
    }

    public void setHint(int resId) {
        setHint(getContext().getResources().getText(resId));
    }

    public CharSequence getHint() {
        return mHint;
    }

    public void setHintTextColor(int color) {
        mHintColor = color;
        invalidate();
    }

    public void setHintTextColor(android.content.res.ColorStateList colors) {
        if (colors != null) setHintTextColor(colors.getDefaultColor());
    }

    public int getCurrentHintTextColor() {
        return mHintColor;
    }

    // IME attachment.

    @Override
    public boolean onCheckIsTextEditor() {
        return isEditable();
    }

    @Override
    public android.view.inputmethod.InputConnection onCreateInputConnection(
            android.view.inputmethod.EditorInfo outAttrs) {
        if (!isEditable()) return null;
        if (outAttrs != null) {
            outAttrs.inputType = mInputType != 0 ? mInputType
                    : android.text.InputType.TYPE_CLASS_TEXT
                    | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE;
            outAttrs.imeOptions = mImeOptions;
            outAttrs.actionId = mImeActionId;
            outAttrs.actionLabel = mImeActionLabel;
            outAttrs.hintText = mHint;
            outAttrs.initialSelStart = getSelectionStart();
            outAttrs.initialSelEnd = getSelectionEnd();
            // A single-line field's Return key is an action; default it to Done.
            if (!isMultiLineInput()
                    && (outAttrs.imeOptions & android.view.inputmethod.EditorInfo.IME_MASK_ACTION)
                            == android.view.inputmethod.EditorInfo.IME_ACTION_UNSPECIFIED) {
                outAttrs.imeOptions |= android.view.inputmethod.EditorInfo.IME_ACTION_DONE;
            }
        }
        return new EditableInputConnection(this);
    }

    @Override
    protected void onFocusChanged(boolean gainFocus, int direction,
                                  android.graphics.Rect previouslyFocusedRect) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
        refresh();
        if (!isEditable()) return;
        final android.view.inputmethod.InputMethodManager imm =
                android.view.inputmethod.InputMethodManager.getInstance();
        if (gainFocus) {
            // Typed text goes to the focused editor from now on.
            imm.restartInput(this);
            return;
        }
        // Focus left this editor: stop routing text to it, and put the keyboard away
        // unless another editor took focus (it brings the keyboard itself).
        imm.detach(this);
        final View now = getFocusedView();
        if (!(now instanceof TextView && ((TextView) now).isEditable())) {
            imm.hideSoftInputFromWindow(getWindowToken(), 0);
        }
    }

    public void setImeOptions(int imeOptions) {
        mImeOptions = imeOptions;
    }

    public int getImeOptions() {
        return mImeOptions;
    }

    public void setPrivateImeOptions(String type) {
        mPrivateImeOptions = type;
    }

    public String getPrivateImeOptions() {
        return mPrivateImeOptions;
    }

    public void setImeActionLabel(CharSequence label, int actionId) {
        mImeActionLabel = label;
        mImeActionId = actionId;
    }

    public CharSequence getImeActionLabel() {
        return mImeActionLabel;
    }

    public int getImeActionId() {
        return mImeActionId;
    }

    public void setRawInputType(int type) {
        mInputType = type;
    }

    public void setSingleLine() {
        setSingleLine(true);
    }

    public void setSingleLine(boolean singleLine) {
        mSingleLine = singleLine;
        if (singleLine) setLines(1);
        else setMaxLines(Integer.MAX_VALUE);
        mHorizontallyScrolling = singleLine;
        requestLayout();
        invalidate();
    }

    public void setLines(int lines) {
        mMinLines = mMaxLines = lines;
        mMinHeightInLines = mMaxHeightInLines = true;
        requestLayout();
        invalidate();
    }

    public int getMaxLines() {
        return mMaxHeightInLines ? mMaxLines : -1;
    }

    public int getMinLines() {
        return mMinHeightInLines ? mMinLines : -1;
    }

    public void setMinLines(int minlines) {
        mMinLines = minlines;
        mMinHeightInLines = true;
        requestLayout();
        invalidate();
    }

    public void setMaxLines(int maxlines) {
        mMaxLines = maxlines;
        mMaxHeightInLines = true;
        requestLayout();
        invalidate();
    }

    public void setHorizontallyScrolling(boolean whether) {
        mHorizontallyScrolling = whether;
        requestLayout();
        invalidate();
    }

    public void setSelectAllOnFocus(boolean selectAllOnFocus) {
        mSelectAllOnFocus = selectAllOnFocus;
    }

    public void setCursorVisible(boolean visible) {
        mCursorVisible = visible;
    }

    public boolean isCursorVisible() {
        return mCursorVisible;
    }

    public void setTextIsSelectable(boolean selectable) {
        mTextIsSelectable = selectable;
    }

    public boolean isTextSelectable() {
        return mTextIsSelectable;
    }

    public int length() {
        return mText != null ? mText.length() : 0;
    }

    public void setHighlightColor(int color) {
        mHighlightColor = color;
    }

    public int getHighlightColor() {
        return mHighlightColor;
    }

    public void setIncludeFontPadding(boolean includepad) {
        mIncludeFontPadding = includepad;
    }

    public boolean getIncludeFontPadding() {
        return mIncludeFontPadding;
    }

    public void setAllCaps(boolean allCaps) {
        mAllCaps = allCaps;
        requestLayout();
        invalidate();
    }

    public boolean isAllCaps() {
        return mAllCaps;
    }

    public void setLineSpacing(float add, float mult) {
        mSpacingAdd = add;
        mSpacingMult = mult;
        requestLayout();
        invalidate();
    }

    public void setMinWidth(int minPixels) { mMinWidth = minPixels; requestLayout(); }
    public void setMaxWidth(int maxPixels) { mMaxWidth = maxPixels; requestLayout(); }
    public void setMinHeight(int minPixels) { mMinHeight = minPixels; mMinHeightInLines = false; requestLayout(); invalidate(); }
    public void setMaxHeight(int maxPixels) { mMaxHeight = maxPixels; mMaxHeightInLines = false; requestLayout(); invalidate(); }
    public int getMinWidth() { return mMinWidth; }
    public int getMaxWidth() { return mMaxWidth; }
    public int getMinHeight() { return mMinHeightInLines ? -1 : mMinHeight; }
    public int getMaxHeight() { return mMaxHeightInLines ? -1 : mMaxHeight; }

    public void setWidth(int pixels) {
        mMinWidth = pixels;
        mMaxWidth = pixels;
        requestLayout();
    }

    public void setHeight(int pixels) {
        mMinHeight = pixels;
        mMaxHeight = pixels;
        mMinHeightInLines = mMaxHeightInLines = false;
        requestLayout();
        invalidate();
    }

    public void setTextScaleX(float size) {}
    public float getTextScaleX() { return 1f; }
    public void setLetterSpacing(float letterSpacing) {}
    public float getLetterSpacing() { return 0f; }

    public int getLineCount() {
        final int width = getWidth() > 0 ? getWidth() : getMeasuredWidth();
        layoutLines(Math.max(0, width - getCompoundPaddingLeft() - getCompoundPaddingRight()));
        return mLineText.length;
    }

    public int getLineHeight() {
        return Math.round(lineHeight());
    }

    public float getLineSpacingMultiplier() {
        return mSpacingMult;
    }

    public float getLineSpacingExtra() {
        return mSpacingAdd;
    }

    public void setCompoundDrawables(android.graphics.drawable.Drawable left,
            android.graphics.drawable.Drawable top, android.graphics.drawable.Drawable right,
            android.graphics.drawable.Drawable bottom) {
        mDrawables[0] = left;
        mDrawables[1] = top;
        mDrawables[2] = right;
        mDrawables[3] = bottom;
        for (android.graphics.drawable.Drawable d : mDrawables) {
            if (d != null) {
                d.setCallback(this);
                if (d.isStateful()) d.setState(getDrawableState());
            }
        }
        applyCompoundDrawableTint();
        requestLayout();
        invalidate();
    }

    public void setCompoundDrawableTintList(android.content.res.ColorStateList tint) {
        mCompoundDrawableTintList = tint;
        applyCompoundDrawableTint();
        invalidate();
    }

    public android.content.res.ColorStateList getCompoundDrawableTintList() {
        return mCompoundDrawableTintList;
    }

    public void setCompoundDrawableTintMode(android.graphics.PorterDuff.Mode tintMode) {
        mCompoundDrawableTintMode = tintMode != null
                ? tintMode : android.graphics.PorterDuff.Mode.SRC_IN;
        applyCompoundDrawableTint();
        invalidate();
    }

    public android.graphics.PorterDuff.Mode getCompoundDrawableTintMode() {
        return mCompoundDrawableTintMode;
    }

    private void applyCompoundDrawableTint() {
        for (android.graphics.drawable.Drawable d : mDrawables) {
            if (d == null) continue;
            if (mCompoundDrawableTintList != null) {
                d.setColorFilter(new android.graphics.PorterDuffColorFilter(
                        mCompoundDrawableTintList.getDefaultColor(), mCompoundDrawableTintMode));
            } else {
                d.clearColorFilter();
            }
        }
    }

    public void setCompoundDrawablesRelative(android.graphics.drawable.Drawable start,
            android.graphics.drawable.Drawable top, android.graphics.drawable.Drawable end,
            android.graphics.drawable.Drawable bottom) {
        setCompoundDrawables(start, top, end, bottom);
    }

    private static android.graphics.drawable.Drawable withIntrinsicBounds(android.graphics.drawable.Drawable d) {
        if (d != null) d.setBounds(0, 0, Math.max(0, d.getIntrinsicWidth()), Math.max(0, d.getIntrinsicHeight()));
        return d;
    }

    private android.graphics.drawable.Drawable drawableFor(int resId) {
        if (resId == 0) return null;
        try {
            return getContext().getDrawable(resId);
        } catch (RuntimeException e) {
            return null;
        }
    }

    public void setCompoundDrawablesWithIntrinsicBounds(int left, int top, int right, int bottom) {
        setCompoundDrawablesWithIntrinsicBounds(drawableFor(left), drawableFor(top),
                drawableFor(right), drawableFor(bottom));
    }

    public void setCompoundDrawablesWithIntrinsicBounds(android.graphics.drawable.Drawable left,
            android.graphics.drawable.Drawable top, android.graphics.drawable.Drawable right,
            android.graphics.drawable.Drawable bottom) {
        setCompoundDrawables(withIntrinsicBounds(left), withIntrinsicBounds(top),
                withIntrinsicBounds(right), withIntrinsicBounds(bottom));
    }

    public void setCompoundDrawablesRelativeWithIntrinsicBounds(int start, int top, int end, int bottom) {
        setCompoundDrawablesWithIntrinsicBounds(start, top, end, bottom);
    }

    public void setCompoundDrawablesRelativeWithIntrinsicBounds(android.graphics.drawable.Drawable start,
            android.graphics.drawable.Drawable top, android.graphics.drawable.Drawable end,
            android.graphics.drawable.Drawable bottom) {
        setCompoundDrawablesWithIntrinsicBounds(start, top, end, bottom);
    }

    public android.graphics.drawable.Drawable[] getCompoundDrawables() {
        return mDrawables.clone();
    }

    public android.graphics.drawable.Drawable[] getCompoundDrawablesRelative() {
        return mDrawables.clone();
    }

    public void setCompoundDrawablePadding(int pad) {
        mDrawablePadding = pad;
        requestLayout();
        invalidate();
    }

    public int getCompoundDrawablePadding() { return mDrawablePadding; }

    @Override
    public void invalidateDrawable(android.graphics.drawable.Drawable drawable) {
        if (mDrawables != null) {
            for (android.graphics.drawable.Drawable d : mDrawables) {
                if (d == drawable) {
                    invalidate();
                    return;
                }
            }
        }
        super.invalidateDrawable(drawable);
    }

    @Override
    protected boolean verifyDrawable(android.graphics.drawable.Drawable who) {
        if (mDrawables != null) {
            for (android.graphics.drawable.Drawable d : mDrawables) {
                if (d == who) return true;
            }
        }
        return super.verifyDrawable(who);
    }

    public void setTypeface(android.graphics.Typeface tf) {}
    public void setTypeface(android.graphics.Typeface tf, int style) {}
    public android.graphics.Typeface getTypeface() { return null; }
    public void setShadowLayer(float radius, float dx, float dy, int color) {}
    public void setCustomSelectionActionModeCallback(android.view.ActionMode.Callback actionModeCallback) {}
    public android.view.ActionMode.Callback getCustomSelectionActionModeCallback() { return null; }
    public void setCustomInsertionActionModeCallback(android.view.ActionMode.Callback actionModeCallback) {}
    public android.view.ActionMode.Callback getCustomInsertionActionModeCallback() { return null; }
    private android.text.TextUtils.TruncateAt mTruncateAt;

    public void setEllipsize(android.text.TextUtils.TruncateAt where) {
        mTruncateAt = where;
        mEllipsize = where == null ? -1 : ELLIPSIZE_END;
        requestLayout();
        invalidate();
    }

    public android.text.TextUtils.TruncateAt getEllipsize() { return mTruncateAt; }
    public void setTextKeepState(CharSequence text) { setText(text); }
}
