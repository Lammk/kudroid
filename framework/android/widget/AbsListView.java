package android.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.view.MotionEvent;
import android.view.View;
import android.database.DataSetObserver;

/**
 * android.widget.AbsListView — general part of ListView/GridView: holds adapter,
 * Generate child view from adapter, dispatch item click according to touch coordinates.
 */
public abstract class AbsListView extends AdapterView<ListAdapter> {
    /** Scroll status: stationary. */
    public static final int SCROLL_STATE_IDLE = 0;
    public static final int SCROLL_STATE_TOUCH_SCROLL = 1;
    public static final int SCROLL_STATE_FLING = 2;

    public static final int CHOICE_MODE_NONE = 0;
    public static final int CHOICE_MODE_SINGLE = 1;
    public static final int CHOICE_MODE_MULTIPLE = 2;

    /**
     * Callback when the list scrolls.
     */
    public interface OnScrollListener {
        int SCROLL_STATE_IDLE = 0;
        int SCROLL_STATE_TOUCH_SCROLL = 1;
        int SCROLL_STATE_FLING = 2;

        void onScrollStateChanged(AbsListView view, int scrollState);

        void onScroll(AbsListView view, int firstVisibleItem, int visibleItemCount,
                      int totalItemCount);
    }

    /**
     * Multi-item selector in ActionMode mode.
     */
    public interface MultiChoiceModeListener extends android.view.ActionMode.Callback {
        void onItemCheckedStateChanged(android.view.ActionMode mode, int position, long id,
                                       boolean checked);
    }

    private ListAdapter mAdapter;
    private OnScrollListener mOnScrollListener;
    private int mChoiceMode = CHOICE_MODE_NONE;
    private int mItemHeight = 0;

    /** Re-layout when the adapter reports data changes — without this, the list will remain static. */
    private final DataSetObserver mObserver = new DataSetObserver() {
        @Override
        public void onChanged() {
            rebuildChildren();
        }

        @Override
        public void onInvalidated() {
            removeAllViews();
        }
    };

    public AbsListView(Context context) {
        super(context);
    }

    @Override
    public ListAdapter getAdapter() {
        return mAdapter;
    }

    @Override
    public void setAdapter(ListAdapter adapter) {
        if (mAdapter != null) {
            mAdapter.unregisterDataSetObserver(mObserver);
        }
        mAdapter = adapter;
        if (mAdapter != null) {
            mAdapter.registerDataSetObserver(mObserver);
        }
        rebuildChildren();
    }

    public void setOnScrollListener(OnScrollListener l) {
        mOnScrollListener = l;
    }

    public void setChoiceMode(int choiceMode) {
        mChoiceMode = choiceMode;
    }

    public int getChoiceMode() {
        return mChoiceMode;
    }

    /** Regenerate all child views from the adapter (no view recycling). */
    protected void rebuildChildren() {
        removeAllViews();
        if (mAdapter == null) return;
        final int n = mAdapter.getCount();
        for (int i = 0; i < n; ++i) {
            View child = mAdapter.getView(i, null, this);
            if (child != null) super.addView(child);
        }
        if (mOnScrollListener != null) {
            mOnScrollListener.onScroll(this, 0, n, n);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (event == null || mAdapter == null) return false;
        if (event.getAction() != MotionEvent.ACTION_UP) return true;
        final int position = pointToPosition((int) event.getX(), (int) event.getY());
        if (position == INVALID_POSITION) return true;
        return performItemClick(getChildAt(position), position,
                                mAdapter.getItemId(position));
    }

    /** Find index items containing points (x,y); INVALID_POSITION if beyond all items. */
    public int pointToPosition(int x, int y) {
        final int n = getChildCount();
        for (int i = 0; i < n; ++i) {
            View child = getChildAt(i);
            if (child == null || child.getVisibility() != View.VISIBLE) continue;
            if (x >= child.getLeft() && x < child.getRight() &&
                y >= child.getTop() && y < child.getBottom()) {
                return i;
            }
        }
        return INVALID_POSITION;
    }

    public long pointToRowId(int x, int y) {
        final int position = pointToPosition(x, y);
        return position == INVALID_POSITION ? INVALID_ROW_ID : getItemIdAtPosition(position);
    }

    @Override
    public void draw(Canvas canvas) {
        if (canvas == null || getVisibility() != View.VISIBLE) return;
        super.draw(canvas);
        final int n = getChildCount();
        for (int i = 0; i < n; ++i) {
            View child = getChildAt(i);
            if (child != null) child.draw(canvas);
        }
    }

    public interface SelectionBoundsAdjuster {
    }

}
