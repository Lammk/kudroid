package android.widget;

import android.content.Context;

public class TableLayout extends LinearLayout {
    public TableLayout(Context context) { this(context, null); }
    public TableLayout(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public TableLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public TableLayout(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public void setColumnCollapsed(int columnIndex, boolean isCollapsed) {}
    public void setColumnStretchable(int columnIndex, boolean isStretchable) {}
    public void setColumnShrinkable(int columnIndex, boolean isShrinkable) {}
}
