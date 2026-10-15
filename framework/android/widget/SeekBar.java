package android.widget;

import android.content.Context;

public class SeekBar extends ProgressBar {
    public interface OnSeekBarChangeListener {
        void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser);
        void onStartTrackingTouch(SeekBar seekBar);
        void onStopTrackingTouch(SeekBar seekBar);
    }
    private OnSeekBarChangeListener mOnSeekBarChangeListener;

    public SeekBar(Context context) { super(context); }
    public void setOnSeekBarChangeListener(OnSeekBarChangeListener l) { mOnSeekBarChangeListener = l; }
}
