package android.widget;

import android.content.Context;

public class Switch extends CompoundButton {
    public Switch(Context context) { this(context, null); }
    public Switch(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public Switch(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public Switch(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
}
