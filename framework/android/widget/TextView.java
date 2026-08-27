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
        super(context);
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

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (mText != null && mText.length() > 0) {
            canvas.drawText(mText.toString(), getLeft(), getTop() + mTextSize, mPaint);
        }
    }
}
