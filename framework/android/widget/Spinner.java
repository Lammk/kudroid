package android.widget;

import android.content.Context;
import android.view.View;

public class Spinner extends AdapterView<SpinnerAdapter> {
    private SpinnerAdapter mAdapter;

    public Spinner(Context context) { this(context, null); }
    public Spinner(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public Spinner(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public Spinner(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public void setAdapter(SpinnerAdapter adapter) { this.mAdapter = adapter; }
    public SpinnerAdapter getAdapter() { return mAdapter; }
    public View getSelectedView() { return null; }
    public void setSelection(int position) {}
    public void setPrompt(CharSequence prompt) {}
    public CharSequence getPrompt() { return ""; }
}
