package android.widget;

import android.content.Context;

public abstract class CompoundButton extends Button {
    private boolean mChecked = false;
    public interface OnCheckedChangeListener {
        void onCheckedChanged(CompoundButton buttonView, boolean isChecked);
    }
    private OnCheckedChangeListener mOnCheckedChangeListener;

    public CompoundButton(Context context) { super(context); }
    public boolean isChecked() { return mChecked; }
    public void setChecked(boolean checked) {
        if (mChecked != checked) {
            mChecked = checked;
            if (mOnCheckedChangeListener != null) mOnCheckedChangeListener.onCheckedChanged(this, mChecked);
        }
    }
    public void toggle() { setChecked(!mChecked); }
    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        mOnCheckedChangeListener = listener;
    }
}
