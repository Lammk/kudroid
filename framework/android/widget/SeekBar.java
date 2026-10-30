package android.widget;

import android.content.Context;

public class SeekBar extends ProgressBar {
    public interface OnSeekBarChangeListener {
        void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser);
        void onStartTrackingTouch(SeekBar seekBar);
        void onStopTrackingTouch(SeekBar seekBar);
    }
    private OnSeekBarChangeListener mOnSeekBarChangeListener;

    public SeekBar(Context context) { this(context, null); }
    public SeekBar(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public SeekBar(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public SeekBar(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public void setOnSeekBarChangeListener(OnSeekBarChangeListener l) { mOnSeekBarChangeListener = l; }
}
