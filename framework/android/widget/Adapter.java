package android.widget;

import android.view.View;
import android.view.ViewGroup;
import android.database.DataSetObserver;

public interface Adapter {
    int getCount();
    Object getItem(int position);
    long getItemId(int position);
    boolean hasStableIds();
    View getView(int position, View convertView, ViewGroup parent);
    int getItemViewType(int position);
    int getViewTypeCount();
    boolean isEmpty();
    void registerDataSetObserver(DataSetObserver observer);
    void unregisterDataSetObserver(DataSetObserver observer);
}
