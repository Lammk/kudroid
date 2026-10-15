package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

public abstract class AdapterView<T extends Adapter> extends ViewGroup {
    public static final int INVALID_POSITION = -1;
    public static final long INVALID_ROW_ID = Long.MIN_VALUE;

    public interface OnItemClickListener {
        void onItemClick(AdapterView<?> parent, View view, int position, long id);
    }
    public interface OnItemLongClickListener {
        boolean onItemLongClick(AdapterView<?> parent, View view, int position, long id);
    }
    public interface OnItemSelectedListener {
        void onItemSelected(AdapterView<?> parent, View view, int position, long id);
        void onNothingSelected(AdapterView<?> parent);
    }

    private OnItemClickListener mOnItemClickListener;
    private OnItemLongClickListener mOnItemLongClickListener;
    private OnItemSelectedListener mOnItemSelectedListener;

    public AdapterView(Context context) { super(context); }
    public abstract T getAdapter();
    public abstract void setAdapter(T adapter);
    public void setOnItemClickListener(OnItemClickListener listener) { mOnItemClickListener = listener; }
    public final OnItemClickListener getOnItemClickListener() { return mOnItemClickListener; }
    public void setOnItemLongClickListener(OnItemLongClickListener listener) { mOnItemLongClickListener = listener; }
    public final OnItemLongClickListener getOnItemLongClickListener() { return mOnItemLongClickListener; }
    public void setOnItemSelectedListener(OnItemSelectedListener listener) { mOnItemSelectedListener = listener; }
    public final OnItemSelectedListener getOnItemSelectedListener() { return mOnItemSelectedListener; }
    public abstract View getSelectedView();
    public abstract void setSelection(int position);
    public int getSelectedItemPosition() { return INVALID_POSITION; }
    public long getSelectedItemId() { return INVALID_ROW_ID; }
    public int getCount() {
        T adapter = getAdapter();
        return adapter != null ? adapter.getCount() : 0;
    }
    public boolean performItemClick(View view, int position, long id) {
        if (mOnItemClickListener != null) {
            mOnItemClickListener.onItemClick(this, view, position, id);
            return true;
        }
        return false;
    }
    public long getItemIdAtPosition(int position) {
        T adapter = getAdapter();
        return (adapter != null && position >= 0) ? adapter.getItemId(position) : INVALID_ROW_ID;
    }
}
