package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

/**
 * Minimal android.widget.TextView implementation.
 *
 * Displays text. For KuDroid's minimal framework, stores text and paint.
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
     * Return the text.
     */
    public CharSequence getText() {
        return mText;
    }

    /**
     * Set the text.
     */
    public void setText(CharSequence text) {
        mText = text != null ? text : "";
    }

    /**
     * Set the text from a resource id.
     */
    public void setText(int resId) {
        mText = "";
    }

    /**
     * Set the text color.
     */
    public void setTextColor(int color) {
        mTextColor = color;
        mPaint.setColor(color);
    }

    /**
     * Set the text size.
     */
    public void setTextSize(float size) {
        mTextSize = size;
        mPaint.setTextSize(size);
    }

    /**
     * Set the gravity.
     */
    public void setGravity(int gravity) {
        mGravity = gravity;
    }

    /**
     * Return the gravity.
     */
    public int getGravity() {
        return mGravity;
    }

    /**
     * Return the text color.
     */
    public int getCurrentTextColor() {
        return mTextColor;
    }

    /**
     * Return the text size.
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
