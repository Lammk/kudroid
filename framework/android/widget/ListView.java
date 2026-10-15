package android.widget;

import android.content.Context;
import android.view.View;

public class ListView extends AdapterView<ListAdapter> {
    private ListAdapter mAdapter;

    public ListView(Context context) { super(context); }
    public ListAdapter getAdapter() { return mAdapter; }
    public void setAdapter(ListAdapter adapter) { mAdapter = adapter; }
    public View getSelectedView() { return null; }
    public void setSelection(int position) {}
}
