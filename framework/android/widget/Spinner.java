package android.widget;

import android.content.Context;
import android.view.View;

public class Spinner extends AdapterView<SpinnerAdapter> {
    private SpinnerAdapter mAdapter;

    public Spinner(Context context) { super(context); }
    public void setAdapter(SpinnerAdapter adapter) { this.mAdapter = adapter; }
    public SpinnerAdapter getAdapter() { return mAdapter; }
    public View getSelectedView() { return null; }
    public void setSelection(int position) {}
    public void setPrompt(CharSequence prompt) {}
    public CharSequence getPrompt() { return ""; }
}
