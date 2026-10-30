package android.widget;

import android.content.Context;
import android.graphics.drawable.Drawable;

/** A TextView with a checked state; the check mark drawable is kept but not drawn. */
public class CheckedTextView extends TextView implements Checkable {
    private boolean mChecked;
    private Drawable mCheckMark;

    public CheckedTextView(Context context) { this(context, null); }
    public CheckedTextView(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public CheckedTextView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public CheckedTextView(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    public void toggle() { setChecked(!mChecked); }
    public boolean isChecked() { return mChecked; }

    public void setChecked(boolean checked) {
        if (mChecked != checked) {
            mChecked = checked;
            refreshDrawableState();
        }
    }

    public void setCheckMarkDrawable(int resId) {
        setCheckMarkDrawable(resId != 0 ? getContext().getDrawable(resId) : null);
    }

    public void setCheckMarkDrawable(Drawable d) { mCheckMark = d; }
    public Drawable getCheckMarkDrawable() { return mCheckMark; }
    public void setCheckMarkTintList(android.content.res.ColorStateList tint) {}
    public void setCheckMarkTintMode(android.graphics.PorterDuff.Mode tintMode) {}
}
