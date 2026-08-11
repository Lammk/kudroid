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
    private CharSequence mText = "";
    private int mTextColor = 0xFF000000;
    private float mTextSize = 14.0f;
    private int mGravity = 0;
    private Paint mPaint;

    public TextView(Context context) {
        super(context);
        mPaint = new Paint();
        mPaint.setColor(mTextColor);
        mPaint.setTextSize(mTextSize);
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
        mText = text != null ? text : "";
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
