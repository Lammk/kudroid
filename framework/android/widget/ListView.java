package android.widget;

import android.content.Context;
import android.view.View;

/**
 * android.widget.ListView — Vertically stacked AbsListView.
 */
public class ListView extends AbsListView {
    private int mDividerHeight = 1;
    private View mEmptyView;

    public ListView(Context context) {
        super(context);
    }

    public void setDividerHeight(int height) {
        mDividerHeight = height;
    }

    public int getDividerHeight() {
        return mDividerHeight;
    }

    public void setEmptyView(View emptyView) {
        mEmptyView = emptyView;
    }

    public View getEmptyView() {
        return mEmptyView;
    }

    /** Header/footer is not supported yet — add it as a regular item so the view will still be visible. */
    public void addHeaderView(View v) {
        if (v != null) super.addView(v);
    }

    public void addHeaderView(View v, Object data, boolean isSelectable) {
        addHeaderView(v);
    }

    public void addFooterView(View v) {
        if (v != null) super.addView(v);
    }

    public void addFooterView(View v, Object data, boolean isSelectable) {
        addFooterView(v);
    }

    public boolean removeHeaderView(View v) {
        removeView(v);
        return true;
    }

    public boolean removeFooterView(View v) {
        removeView(v);
        return true;
    }

    public int getHeaderViewsCount() {
        return 0;
    }

    public int getFooterViewsCount() {
        return 0;
    }

    public void smoothScrollToPosition(int position) {
        setSelection(position);
    }

    public void setSelectionFromTop(int position, int y) {
        setSelection(position);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        final int n = getChildCount();
        int y = t;
        final int itemHeight = n > 0 ? Math.max(1, (b - t) / n) : 0;
        for (int i = 0; i < n; ++i) {
            View child = getChildAt(i);
            if (child == null) continue;
            child.layout(l, y, r, y + itemHeight);
            y += itemHeight + mDividerHeight;
        }
    }
}
