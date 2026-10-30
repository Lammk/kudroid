package android.widget;

import android.content.Context;

public class ToggleButton extends CompoundButton {
    private CharSequence mTextOn = "ON";
    private CharSequence mTextOff = "OFF";

    public ToggleButton(Context context) { this(context, null); }
    public ToggleButton(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public ToggleButton(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public ToggleButton(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    public CharSequence getTextOn() { return mTextOn; }
    public void setTextOn(CharSequence textOn) { mTextOn = textOn; }
    public CharSequence getTextOff() { return mTextOff; }
    public void setTextOff(CharSequence textOff) { mTextOff = textOff; }
}
