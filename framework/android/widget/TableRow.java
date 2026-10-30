package android.widget;

import android.content.Context;
import android.view.View;

public class TableRow extends LinearLayout {
    public TableRow(Context context) { this(context, null); }
    public TableRow(Context context, android.util.AttributeSet attrs) { this(context, attrs, 0); }
    public TableRow(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }
    public TableRow(Context context, android.util.AttributeSet attrs, int defStyleAttr,
            int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }
    public View getVirtualChildAt(int i) { return getChildAt(i); }
    public int getVirtualChildCount() { return getChildCount(); }
}
