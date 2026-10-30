package android.widget;

import android.content.Context;

public abstract class CompoundButton extends Button {
    private boolean mChecked = false;
    public interface OnCheckedChangeListener {
        void onCheckedChanged(CompoundButton buttonView, boolean isChecked);
    }
    private OnCheckedChangeListener mOnCheckedChangeListener;

    public CompoundButton(Context context) { this(context, null); }
    public CompoundButton(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public CompoundButton(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public CompoundButton(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public boolean isChecked() { return mChecked; }
    public void setChecked(boolean checked) {
        if (mChecked != checked) {
            mChecked = checked;
            if (mOnCheckedChangeListener != null) mOnCheckedChangeListener.onCheckedChanged(this, mChecked);
        }
    }
    public void toggle() { setChecked(!mChecked); }
    private android.graphics.drawable.Drawable mButtonDrawable;
    private int mButtonResource;

    public void setButtonDrawable(int resId) {
        if (resId != 0 && resId == mButtonResource) return;
        mButtonResource = resId;
        android.graphics.drawable.Drawable d = null;
        if (mButtonResource != 0) {
            d = getContext().getDrawable(mButtonResource);
        }
        setButtonDrawable(d);
    }

    public void setButtonDrawable(android.graphics.drawable.Drawable drawable) {
        if (mButtonDrawable != drawable) {
            if (mButtonDrawable != null) {
                mButtonDrawable.setCallback(null);
                unscheduleDrawable(mButtonDrawable);
            }
            mButtonDrawable = drawable;
            if (drawable != null) {
                drawable.setCallback(this);
            }
            refreshDrawableState();
            invalidate();
        }
    }

    public android.graphics.drawable.Drawable getButtonDrawable() {
        return mButtonDrawable;
    }

    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        mOnCheckedChangeListener = listener;
    }
}
