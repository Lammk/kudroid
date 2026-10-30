package android.widget;

import android.content.Context;

public class RadioButton extends CompoundButton {
    public RadioButton(Context context) { this(context, null); }
    public RadioButton(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public RadioButton(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public RadioButton(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
}
