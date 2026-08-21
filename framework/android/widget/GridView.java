package android.widget;

import android.content.Context;
import android.view.View;

/**
 * android.widget.GridView — AbsListView xếp con thành lưới.
 */
public class GridView extends AbsListView {
    /** Số cột tự tính theo chiều rộng khả dụng. */
    public static final int AUTO_FIT = -1;

    public static final int NO_STRETCH = 0;
    public static final int STRETCH_SPACING = 1;
    public static final int STRETCH_COLUMN_WIDTH = 2;
    public static final int STRETCH_SPACING_UNIFORM = 3;

    private int mNumColumns = AUTO_FIT;
    private int mColumnWidth = 0;
    private int mHorizontalSpacing = 0;
    private int mVerticalSpacing = 0;
    private int mStretchMode = STRETCH_COLUMN_WIDTH;

    public GridView(Context context) {
        super(context);
    }

    public void setNumColumns(int numColumns) {
        mNumColumns = numColumns;
    }

    public int getNumColumns() {
        return mNumColumns;
    }

    public void setColumnWidth(int columnWidth) {
        mColumnWidth = columnWidth;
    }

    public int getColumnWidth() {
        return mColumnWidth;
    }

    public void setHorizontalSpacing(int spacing) {
        mHorizontalSpacing = spacing;
    }

    public void setVerticalSpacing(int spacing) {
        mVerticalSpacing = spacing;
    }

    public void setStretchMode(int stretchMode) {
        mStretchMode = stretchMode;
    }

    public int getStretchMode() {
        return mStretchMode;
    }

    public void smoothScrollToPosition(int position) {
        setSelection(position);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        final int n = getChildCount();
        if (n == 0) return;
        final int available = Math.max(1, r - l);
        int columns = mNumColumns;
        if (columns == AUTO_FIT) {
            columns = (mColumnWidth > 0) ? Math.max(1, available / mColumnWidth) : 2;
        }
        columns = Math.max(1, columns);
        final int cellW = Math.max(1, (available - (columns - 1) * mHorizontalSpacing) / columns);
        final int rows = (n + columns - 1) / columns;
        final int cellH = Math.max(1, (b - t - (rows - 1) * mVerticalSpacing) / Math.max(1, rows));

        for (int i = 0; i < n; ++i) {
            View child = getChildAt(i);
            if (child == null) continue;
            final int col = i % columns;
            final int row = i / columns;
            final int x = l + col * (cellW + mHorizontalSpacing);
            final int y = t + row * (cellH + mVerticalSpacing);
            child.layout(x, y, x + cellW, y + cellH);
        }
    }
}
