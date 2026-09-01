package android.widget;

import android.content.Context;
import android.graphics.Canvas;
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
    private float mTextSize = 14.0f;
    private int mGravity = 0;
    private Paint mPaint;
    private final java.util.List<android.text.TextWatcher> mWatchers =
            new java.util.ArrayList<android.text.TextWatcher>();
    private OnEditorActionListener mOnEditorActionListener;

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
        mPaint = new Paint();
        mPaint.setColor(mTextColor);
        mPaint.setTextSize(mTextSize);
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

    /** Returns false if no listener consumes the action. */
    public boolean onEditorAction(int actionCode) {
        if (mOnEditorActionListener != null) {
            return mOnEditorActionListener.onEditorAction(this, actionCode, null);
        }
        return false;
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
        final CharSequence old = mText;
        final CharSequence next = text != null ? text : "";
        final int oldLen = old.length();
        for (int i = mWatchers.size() - 1; i >= 0; --i) {
            mWatchers.get(i).beforeTextChanged(old, 0, oldLen, next.length());
        }
        mText = next;
        for (int i = mWatchers.size() - 1; i >= 0; --i) {
            mWatchers.get(i).onTextChanged(mText, 0, oldLen, mText.length());
        }
        // afterTextChanged get Editable; Only called when the actual text is Editable
        // (EditText), and plain TextView is ignored just like Android.
        if (mText instanceof android.text.Editable) {
            for (int i = mWatchers.size() - 1; i >= 0; --i) {
                mWatchers.get(i).afterTextChanged((android.text.Editable) mText);
            }
        }
    }

    /**
     * set text from a resource id.
     */
    public void setText(int resId) {
        mText = "";
    }

    // ── input filters ───────────────────────────────────────────────────────
    //
    // An app sets these to cap length or force case, then reads them back to modify
    // the chain. Returning null from getFilters — which is what no storage at all
    // amounts to — breaks the read-modify-write idiom apps use to add one filter
    // without dropping the others.
    private android.text.InputFilter[] mFilters = new android.text.InputFilter[0];

    public void setFilters(android.text.InputFilter[] filters) {
        mFilters = filters != null ? filters : new android.text.InputFilter[0];
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
        mTextColor = color;
        mPaint.setColor(color);
    }

    /**
     * set text size.
     */
    public void setTextSize(float size) {
        mTextSize = size;
        mPaint.setTextSize(size);
    }

    /**
     * set gravity.
     */
    public void setGravity(int gravity) {
        mGravity = gravity;
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

    /**
     * Report the space the text needs. Without this every TextView measured to
     * zero, so a LinearLayout stacked them all at the same y and the lines drew on
     * top of each other in the corner.
     *
     * Height is one line plus a little leading; width comes from Paint.measureText.
     * Both are then clamped by whatever the parent allows.
     */
    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        final String text = mText != null ? mText.toString() : "";
        int wantWidth = (int) Math.ceil(mPaint.measureText(text));
        int wantHeight = (int) Math.ceil(mTextSize * LINE_SPACING);

        // Multi-line: setText() strings in the fallback UI start with '\n'.
        int lines = 1;
        for (int i = 0; i < text.length(); i++) {
            if (text.charAt(i) == '\n') lines++;
        }
        if (lines > 1) {
            wantHeight = (int) Math.ceil(mTextSize * LINE_SPACING * lines);
            int widest = 0;
            int lineStart = 0;
            for (int i = 0; i <= text.length(); i++) {
                if (i == text.length() || text.charAt(i) == '\n') {
                    int w = (int) Math.ceil(mPaint.measureText(text.substring(lineStart, i)));
                    if (w > widest) widest = w;
                    lineStart = i + 1;
                }
            }
            wantWidth = widest;
        }

        setMeasuredDimension(resolveSize(wantWidth, widthMeasureSpec),
                             resolveSize(wantHeight, heightMeasureSpec));
    }

    /** Clamp a desired size against the parent's constraint. */
    private static int resolveSize(int want, int measureSpec) {
        final int mode = MeasureSpec.getMode(measureSpec);
        final int size = MeasureSpec.getSize(measureSpec);
        if (mode == MeasureSpec.EXACTLY) return size;
        if (mode == MeasureSpec.AT_MOST) return Math.min(want, size);
        return want;
    }

    /** Gap between baselines as a multiple of text size, matching Android's default. */
    private static final float LINE_SPACING = 1.35f;

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (mText == null || mText.length() == 0 || canvas == null) return;

        // Draw relative to this view's own bounds, which the parent set during
        // layout. drawText takes a BASELINE y, so the first line sits one text
        // size below the top edge rather than at it.
        final String text = mText.toString();
        final float lineHeight = mTextSize * LINE_SPACING;
        float baseline = getTop() + mTextSize;
        int lineStart = 0;
        for (int i = 0; i <= text.length(); i++) {
            if (i == text.length() || text.charAt(i) == '\n') {
                if (i > lineStart) {
                    canvas.drawText(text.substring(lineStart, i), getLeft(), baseline, mPaint);
                }
                baseline += lineHeight;
                lineStart = i + 1;
            }
        }
    }
}
