package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

public class AutoCompleteTextView extends EditText {
    private AdapterView.OnItemClickListener mItemClickListener;
    private AdapterView.OnItemSelectedListener mItemSelectedListener;

    public AutoCompleteTextView(Context context) { this(context, null); }
    public AutoCompleteTextView(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public AutoCompleteTextView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public AutoCompleteTextView(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    private int mDropDownAnchorId = View.NO_ID;
    private int mDropDownWidth = ViewGroup.LayoutParams.WRAP_CONTENT;
    private int mDropDownHeight = ViewGroup.LayoutParams.WRAP_CONTENT;
    private int mDropDownHorizontalOffset = 0;
    private int mDropDownVerticalOffset = 0;
    private android.graphics.drawable.Drawable mDropDownBackground;

    public void setThreshold(int threshold) {}
    public int getThreshold() { return 1; }
    public void setAdapter(ListAdapter adapter) {}
    public ListAdapter getAdapter() { return null; }
    public void showDropDown() {}
    public void dismissDropDown() {}
    public boolean isPopupShowing() { return false; }

    public int getDropDownAnchor() { return mDropDownAnchorId; }
    public void setDropDownAnchor(int id) { mDropDownAnchorId = id; }
    public int getDropDownWidth() { return mDropDownWidth; }
    public void setDropDownWidth(int width) { mDropDownWidth = width; }
    public int getDropDownHeight() { return mDropDownHeight; }
    public void setDropDownHeight(int height) { mDropDownHeight = height; }
    public int getDropDownHorizontalOffset() { return mDropDownHorizontalOffset; }
    public void setDropDownHorizontalOffset(int offset) { mDropDownHorizontalOffset = offset; }
    public int getDropDownVerticalOffset() { return mDropDownVerticalOffset; }
    public void setDropDownVerticalOffset(int offset) { mDropDownVerticalOffset = offset; }
    public android.graphics.drawable.Drawable getDropDownBackground() { return mDropDownBackground; }
    public void setDropDownBackgroundDrawable(android.graphics.drawable.Drawable d) { mDropDownBackground = d; }
    public void setDropDownBackgroundResource(int id) {
        if (id != 0) setDropDownBackgroundDrawable(getContext().getDrawable(id));
    }

    public void setOnItemClickListener(AdapterView.OnItemClickListener listener) {
        mItemClickListener = listener;
    }

    public void setOnItemSelectedListener(AdapterView.OnItemSelectedListener listener) {
        mItemSelectedListener = listener;
    }

    @Deprecated
    public AdapterView.OnItemClickListener getItemClickListener() {
        return mItemClickListener;
    }

    @Deprecated
    public AdapterView.OnItemSelectedListener getItemSelectedListener() {
        return mItemSelectedListener;
    }

    public AdapterView.OnItemClickListener getOnItemClickListener() {
        return mItemClickListener;
    }

    public AdapterView.OnItemSelectedListener getOnItemSelectedListener() {
        return mItemSelectedListener;
    }
}
