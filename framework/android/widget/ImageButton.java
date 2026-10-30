package android.widget;

import android.content.Context;

public class ImageButton extends ImageView {
    public ImageButton(Context context) { this(context, null); }
    public ImageButton(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public ImageButton(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public ImageButton(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
}
