package android.widget;

import android.content.Context;
import android.view.ViewGroup;

public class GridLayout extends ViewGroup {
    public static final int HORIZONTAL = 0;
    public static final int VERTICAL = 1;

    public GridLayout(Context context) { super(context); }
    public int getRowCount() { return 0; }
    public void setRowCount(int rowCount) {}
    public int getColumnCount() { return 0; }
    public void setColumnCount(int columnCount) {}
    public int getOrientation() { return HORIZONTAL; }
    public void setOrientation(int orientation) {}
}
