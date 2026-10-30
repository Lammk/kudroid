package android.widget;

import android.content.Context;

public class CheckBox extends CompoundButton {
    public CheckBox(Context context) { this(context, null); }
    public CheckBox(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public CheckBox(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public CheckBox(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
}
