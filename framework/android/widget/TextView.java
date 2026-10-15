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
        public android.text.Editable replace(int st, int en, CharSequence source,
                                             int start, int end) {
            final String before = toString();
            super.replace(st, en, source, start, end);
            changed(before, st, en);
            return this;
        }

        @Override
        public android.text.Editable replace(int st, int en, CharSequence text) {
            final String before = toString();
            super.replace(st, en, text);
            changed(before, st, en);
            return this;
        }

        @Override
        public android.text.Editable delete(int st, int en) {
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
        setText("");
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
        final String text = displayText();
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
        if (isEditable()) {
            // An empty field must still be big enough to tap.
            wantWidth = Math.max(wantWidth, (int) Math.ceil(mPaint.measureText(mHint.toString())));
            wantWidth = Math.max(wantWidth, (int) Math.ceil(mTextSize * MIN_EDIT_WIDTH_EM));
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

    /** Narrowest an editable field measures, in text sizes. */
    private static final float MIN_EDIT_WIDTH_EM = 8f;

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
        final String text = mText != null ? mText.toString() : "";
        if (!isPasswordInput()) return text;
        final StringBuilder dots = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) dots.append('•');
        return dots.toString();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (canvas == null) return;

        // Draw relative to this view's own bounds, which the parent set during
        // layout. drawText takes a BASELINE y, so the first line sits one text
        // size below the top edge rather than at it.
        final String text = displayText();
        final float lineHeight = mTextSize * LINE_SPACING;
        if (text.length() > 0) {
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
        } else if (isEditable() && mHint.length() > 0) {
            final Paint hint = new Paint();
            hint.setColor(mHintColor);
            hint.setTextSize(mTextSize);
            canvas.drawText(mHint.toString(), getLeft(), getTop() + mTextSize, hint);
        }

        if (isEditable() && isFocused() && mCursorVisible) {
            // Caret: after the characters before the cursor on its own line.
            final int cursor = Math.min(clampOffset(mSelStart), text.length());
            final int lineStart = text.lastIndexOf('\n', cursor - 1) + 1;
            int line = 0;
            for (int i = 0; i < cursor; i++) {
                if (text.charAt(i) == '\n') line++;
            }
            final float x = getLeft() + mPaint.measureText(text.substring(lineStart, cursor));
            final float top = getTop() + line * lineHeight;
            canvas.drawRect(x, top, x + 2, top + mTextSize * 1.2f, mPaint);
        }
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
    private int mMinLines = -1;
    private int mMaxLines = -1;
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
        mHint = "";
    }

    public CharSequence getHint() {
        return mHint;
    }

    public void setHintTextColor(int color) {
        mHintColor = color;
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
    }

    public void setLines(int lines) {
        mLines = lines;
    }

    public void setMinLines(int minlines) {
        mMinLines = minlines;
    }

    public void setMaxLines(int maxlines) {
        mMaxLines = maxlines;
    }

    public void setHorizontallyScrolling(boolean whether) {
        mHorizontallyScrolling = whether;
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
    }

    public void setLineSpacing(float add, float mult) {
        mSpacingAdd = add;
        mSpacingMult = mult;
    }

    public float getLineSpacingMultiplier() {
        return mSpacingMult;
    }

    public float getLineSpacingExtra() {
        return mSpacingAdd;
    }

    public void setCompoundDrawables(android.graphics.drawable.Drawable left, android.graphics.drawable.Drawable top, android.graphics.drawable.Drawable right, android.graphics.drawable.Drawable bottom) {}
    public void setCompoundDrawablesWithIntrinsicBounds(int left, int top, int right, int bottom) {}
    public void setCompoundDrawablesWithIntrinsicBounds(android.graphics.drawable.Drawable left, android.graphics.drawable.Drawable top, android.graphics.drawable.Drawable right, android.graphics.drawable.Drawable bottom) {}
    public android.graphics.drawable.Drawable[] getCompoundDrawables() {
        return new android.graphics.drawable.Drawable[4];
    }
    public void setCompoundDrawablePadding(int pad) {}
    public int getCompoundDrawablePadding() { return 0; }
    public void setTypeface(android.graphics.Typeface tf) {}
    public void setTypeface(android.graphics.Typeface tf, int style) {}
    public android.graphics.Typeface getTypeface() { return null; }
    public void setShadowLayer(float radius, float dx, float dy, int color) {}
    public void setCustomSelectionActionModeCallback(android.view.ActionMode.Callback actionModeCallback) {}
    public android.view.ActionMode.Callback getCustomSelectionActionModeCallback() { return null; }
    public void setCustomInsertionActionModeCallback(android.view.ActionMode.Callback actionModeCallback) {}
    public android.view.ActionMode.Callback getCustomInsertionActionModeCallback() { return null; }
    public void setEllipsize(android.text.TextUtils.TruncateAt where) {}
    public android.text.TextUtils.TruncateAt getEllipsize() { return null; }
    public void setTextKeepState(CharSequence text) { setText(text); }
}
