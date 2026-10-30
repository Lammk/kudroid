package android.widget;

import android.content.Context;

public class RadioGroup extends LinearLayout {
    public interface OnCheckedChangeListener {
        void onCheckedChanged(RadioGroup group, int checkedId);
    }
    private int mCheckedId = -1;
    private OnCheckedChangeListener mOnCheckedChangeListener;

    public RadioGroup(Context context) { this(context, null); }
    public RadioGroup(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public RadioGroup(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public RadioGroup(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public void check(int id) {
        mCheckedId = id;
        if (mOnCheckedChangeListener != null) mOnCheckedChangeListener.onCheckedChanged(this, mCheckedId);
    }
    public int getCheckedRadioButtonId() { return mCheckedId; }
    public void clearCheck() { check(-1); }
    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        mOnCheckedChangeListener = listener;
    }
}
