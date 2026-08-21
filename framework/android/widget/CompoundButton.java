package android.widget;

/**
 * android.widget.CompoundButton — Button có trạng thái checked (CheckBox, Switch,
 * RadioButton). Khai báo listener mà app dùng phổ biến.
 */
public class CompoundButton extends Button implements Checkable {
    /**
     * Callback khi trạng thái checked đổi.
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
        // Cờ mBroadcasting chặn vòng lặp vô hạn khi listener gọi lại setChecked.
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
