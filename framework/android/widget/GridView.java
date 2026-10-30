package android.widget;

import android.content.Context;
import android.view.View;

public class GridView extends AdapterView<ListAdapter> {
    private ListAdapter mAdapter;

    public GridView(Context context) { this(context, null); }
    public GridView(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public GridView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public GridView(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public ListAdapter getAdapter() { return mAdapter; }
    public void setAdapter(ListAdapter adapter) { mAdapter = adapter; }
    public View getSelectedView() { return null; }
    public void setSelection(int position) {}
    public void setNumColumns(int numColumns) {}
}
