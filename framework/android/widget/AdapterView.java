package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/**
 * android.widget.AdapterView — ViewGroup lấy con từ một Adapter.
 *
 * Đây là superclass của ListView/GridView/Spinner và là nơi khai báo các
 * listener item click mà app dùng nhiều nhất.
 */
public abstract class AdapterView<T extends Adapter> extends ViewGroup {
    /** Trả về khi không có item nào ở vị trí được hỏi. */
    public static final int INVALID_POSITION = -1;
    public static final long INVALID_ROW_ID = Long.MIN_VALUE;
    public static final int ITEM_VIEW_TYPE_IGNORE = -1;
    public static final int ITEM_VIEW_TYPE_HEADER_OR_FOOTER = -2;

    /**
     * Callback khi một item được nhấn.
     */
    public interface OnItemClickListener {
        void onItemClick(AdapterView<?> parent, View view, int position, long id);
    }

    /**
     * Callback khi một item được nhấn giữ. Trả true nếu đã xử lý.
     */
    public interface OnItemLongClickListener {
        boolean onItemLongClick(AdapterView<?> parent, View view, int position, long id);
    }

    /**
     * Callback khi selection đổi (dùng cho Spinner / bàn phím điều hướng).
     */
    public interface OnItemSelectedListener {
        void onItemSelected(AdapterView<?> parent, View view, int position, long id);

        void onNothingSelected(AdapterView<?> parent);
    }

    private OnItemClickListener mOnItemClickListener;
    private OnItemLongClickListener mOnItemLongClickListener;
    private OnItemSelectedListener mOnItemSelectedListener;
    private int mSelectedPosition = INVALID_POSITION;

    public AdapterView(Context context) {
        super(context);
    }

    public abstract T getAdapter();

    public abstract void setAdapter(T adapter);

    public void setOnItemClickListener(OnItemClickListener listener) {
        mOnItemClickListener = listener;
    }

    public OnItemClickListener getOnItemClickListener() {
        return mOnItemClickListener;
    }

    public void setOnItemLongClickListener(OnItemLongClickListener listener) {
        mOnItemLongClickListener = listener;
    }

    public OnItemLongClickListener getOnItemLongClickListener() {
        return mOnItemLongClickListener;
    }

    public void setOnItemSelectedListener(OnItemSelectedListener listener) {
        mOnItemSelectedListener = listener;
    }

    public OnItemSelectedListener getOnItemSelectedListener() {
        return mOnItemSelectedListener;
    }

    public boolean performItemClick(View view, int position, long id) {
        if (mOnItemClickListener != null) {
            mOnItemClickListener.onItemClick(this, view, position, id);
            return true;
        }
        return false;
    }

    /** Dispatch nhấn giữ; trả false để caller fallback sang click thường. */
    public boolean performItemLongClick(View view, int position, long id) {
        if (mOnItemLongClickListener != null) {
            return mOnItemLongClickListener.onItemLongClick(this, view, position, id);
        }
        return false;
    }

    public int getCount() {
        T adapter = getAdapter();
        return adapter == null ? 0 : adapter.getCount();
    }

    public Object getItemAtPosition(int position) {
        T adapter = getAdapter();
        return (adapter == null || position < 0 || position >= adapter.getCount())
                ? null : adapter.getItem(position);
    }

    public long getItemIdAtPosition(int position) {
        T adapter = getAdapter();
        return (adapter == null || position < 0 || position >= adapter.getCount())
                ? INVALID_ROW_ID : adapter.getItemId(position);
    }

    public int getSelectedItemPosition() {
        return mSelectedPosition;
    }

    public long getSelectedItemId() {
        return getItemIdAtPosition(mSelectedPosition);
    }

    public Object getSelectedItem() {
        return getItemAtPosition(mSelectedPosition);
    }

    public void setSelection(int position) {
        if (mSelectedPosition == position) return;
        mSelectedPosition = position;
        if (mOnItemSelectedListener == null) return;
        if (position == INVALID_POSITION) {
            mOnItemSelectedListener.onNothingSelected(this);
        } else {
            mOnItemSelectedListener.onItemSelected(this, getChildAt(position), position,
                                                   getItemIdAtPosition(position));
        }
    }

    public View getSelectedView() {
        return mSelectedPosition == INVALID_POSITION ? null : getChildAt(mSelectedPosition);
    }

    public int getFirstVisiblePosition() {
        return 0;
    }

    public int getLastVisiblePosition() {
        return getCount() - 1;
    }
}
