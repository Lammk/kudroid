package android.widget;

import android.content.Context;
import android.view.View;

public class ListView extends AdapterView<ListAdapter> {
    private ListAdapter mAdapter;

    public ListView(Context context) { this(context, null); }
    public ListView(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public ListView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public ListView(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public ListAdapter getAdapter() { return mAdapter; }
    public void setAdapter(ListAdapter adapter) { mAdapter = adapter; }
    public View getSelectedView() { return null; }
    public void setSelection(int position) {}
}
