package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

/**
 * triển khai android.widget.textview tối thiểu.
 *
 * hiển thị văn bản. đối với khuôn khổ tối thiểu của kudroid, lưu trữ văn bản và paint.
 */
public class TextView extends View {
    /**
     * Callback khi user nhấn action key trên bàn phím mềm (Done/Search/...).
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

    /** Trả false nếu không có listener nào tiêu thụ action. */
    public boolean onEditorAction(int actionCode) {
        if (mOnEditorActionListener != null) {
            return mOnEditorActionListener.onEditorAction(this, actionCode, null);
        }
        return false;
    }

    /**
     * trả về văn bản.
     */
    public CharSequence getText() {
        return mText;
    }

    /**
     * thiết lập văn bản.
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
        // afterTextChanged nhận Editable; chỉ gọi khi text thật là Editable
        // (EditText), còn TextView thuần thì bỏ qua đúng như Android.
        if (mText instanceof android.text.Editable) {
            for (int i = mWatchers.size() - 1; i >= 0; --i) {
                mWatchers.get(i).afterTextChanged((android.text.Editable) mText);
            }
        }
    }

    /**
     * thiết lập văn bản từ một id tài nguyên.
     */
    public void setText(int resId) {
        mText = "";
    }

    /**
     * thiết lập màu văn bản.
     */
    public void setTextColor(int color) {
        mTextColor = color;
        mPaint.setColor(color);
    }

    /**
     * thiết lập kích thước văn bản.
     */
    public void setTextSize(float size) {
        mTextSize = size;
        mPaint.setTextSize(size);
    }

    /**
     * thiết lập trọng lực (gravity).
     */
    public void setGravity(int gravity) {
        mGravity = gravity;
    }

    /**
     * trả về trọng lực (gravity).
     */
    public int getGravity() {
        return mGravity;
    }

    /**
     * trả về màu văn bản.
     */
    public int getCurrentTextColor() {
        return mTextColor;
    }

    /**
     * trả về kích thước văn bản.
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
