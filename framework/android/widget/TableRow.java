package android.widget;

import android.content.Context;
import android.view.View;

public class TableRow extends LinearLayout {
    public TableRow(Context context) { super(context); }
    public View getVirtualChildAt(int i) { return getChildAt(i); }
    public int getVirtualChildCount() { return getChildCount(); }
}
