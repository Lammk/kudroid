package android.widget;

import android.content.Context;

/**
 * minimal android.widget.button implementation.
 *
 * a clickable button. for kudroid minimal framework, extend textview.
 */
public class Button extends TextView {
    public Button(Context context) {
        this(context, null);
    }

    public Button(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public Button(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public Button(Context context, android.util.AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (getGravity() == 0) setGravity(android.view.Gravity.CENTER);
    }

    /** A button the theme gave no background still reads as one. */
    @Override
    protected void onDraw(android.graphics.Canvas canvas) {
        if (canvas != null && getBackground() == null) {
            android.graphics.Paint p = new android.graphics.Paint();
            p.setColor(0xFF2C2C2C);
            canvas.drawRect(0, 0, getWidth(), getHeight(), p);
        }
        super.onDraw(canvas);
    }
}
