package android.widget;

import android.content.Context;

public class AutoCompleteTextView extends EditText {
    public AutoCompleteTextView(Context context) { super(context); }
    public void setThreshold(int threshold) {}
    public int getThreshold() { return 1; }
    public void setAdapter(ListAdapter adapter) {}
    public ListAdapter getAdapter() { return null; }
    public void showDropDown() {}
    public void dismissDropDown() {}
    public boolean isPopupShowing() { return false; }
}
