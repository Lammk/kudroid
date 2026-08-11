package android.widget;

import android.content.Context;

/**
 * triển khai android.widget.button tối thiểu.
 *
 * một nút có thể nhấp. đối với khuôn khổ tối thiểu của kudroid, mở rộng textview.
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
}
