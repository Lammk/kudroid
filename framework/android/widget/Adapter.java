package android.widget;

import android.view.View;
import android.view.ViewGroup;
import android.database.DataSetObserver;

/**
 * android.widget.Adapter — cầu nối giữa dữ liệu và AdapterView.
 */
public interface Adapter {
    /** Trả về từ getItemViewType khi view không nên được tái sử dụng. */
    public static final int IGNORE_ITEM_VIEW_TYPE = -1;
    public static final int NO_SELECTION = Integer.MIN_VALUE;

    void registerDataSetObserver(DataSetObserver observer);

    void unregisterDataSetObserver(DataSetObserver observer);

    int getCount();

    Object getItem(int position);

    long getItemId(int position);

    boolean hasStableIds();

    View getView(int position, View convertView, ViewGroup parent);

    int getItemViewType(int position);

    int getViewTypeCount();

    boolean isEmpty();
}
