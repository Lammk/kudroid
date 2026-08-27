package android.widget;

import android.content.Context;

/**
 * minimal android.widget.button implementation.
 *
 * a clickable button. for kudroid minimal framework, extend textview.
 */
public class Button extends TextView {
    public Button(Context context) {
        super(context);
    }

    public Button(Context context, android.util.AttributeSet attrs) {
        super(context);
    }

    public Button(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        super(context);
    }

    @Override
    protected void onDraw(android.graphics.Canvas canvas) {
        if (canvas != null) {
            android.graphics.Paint p = new android.graphics.Paint();
            p.setColor(0xFF2C2C2C);
            canvas.drawRect(getLeft(), getTop(), getRight(), getBottom(), p);
        }
        super.onDraw(canvas);
    }
}
