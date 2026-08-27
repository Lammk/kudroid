package android.widget;

/**
 * android.widget.CompoundButton — Button with checked state (CheckBox, Switch,
 *RadioButton). Declare the listener that the app commonly uses.
 */
public class CompoundButton extends Button implements Checkable {
    /**
     * Callback when checked status changes.
     */
    public interface OnCheckedChangeListener {
        void onCheckedChanged(CompoundButton buttonView, boolean isChecked);
    }

    private boolean mChecked;
    private boolean mBroadcasting;
    private OnCheckedChangeListener mOnCheckedChangeListener;

    public CompoundButton(android.content.Context context) {
        super(context);
    }

    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        mOnCheckedChangeListener = listener;
    }

    public void setChecked(boolean checked) {
        if (mChecked == checked) return;
        mChecked = checked;
        // The mBroadcasting flag prevents an infinite loop when the listener calls setChecked again.
        if (mBroadcasting) return;
        mBroadcasting = true;
        if (mOnCheckedChangeListener != null) {
            mOnCheckedChangeListener.onCheckedChanged(this, mChecked);
        }
        mBroadcasting = false;
    }

    public boolean isChecked() {
        return mChecked;
    }

    public void toggle() {
        setChecked(!mChecked);
    }

    @Override
    public boolean performClick() {
        toggle();
        return super.performClick();
    }
}
