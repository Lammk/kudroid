package android.widget;

import android.content.Context;
import android.view.ViewGroup;

public class GridLayout extends ViewGroup {
    public static final int HORIZONTAL = 0;
    public static final int VERTICAL = 1;

    public GridLayout(Context context) { this(context, null); }
    public GridLayout(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public GridLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public GridLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public int getRowCount() { return 0; }
    public void setRowCount(int rowCount) {}
    public int getColumnCount() { return 0; }
    public void setColumnCount(int columnCount) {}
    public int getOrientation() { return HORIZONTAL; }
    public void setOrientation(int orientation) {}
}
