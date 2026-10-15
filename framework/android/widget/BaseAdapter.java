package android.widget;

import android.view.View;
import android.view.ViewGroup;
import android.database.DataSetObserver;
import java.util.ArrayList;

public abstract class BaseAdapter implements ListAdapter {
    private final ArrayList<DataSetObserver> mObservers = new ArrayList<DataSetObserver>();

    public boolean hasStableIds() { return false; }
    public void registerDataSetObserver(DataSetObserver observer) { mObservers.add(observer); }
    public void unregisterDataSetObserver(DataSetObserver observer) { mObservers.remove(observer); }
    public void notifyDataSetChanged() {
        for (DataSetObserver obs : mObservers) obs.onChanged();
    }
    public void notifyDataSetInvalidated() {
        for (DataSetObserver obs : mObservers) obs.onInvalidated();
    }
    public boolean areAllItemsEnabled() { return true; }
    public boolean isEnabled(int position) { return true; }
    public int getItemViewType(int position) { return 0; }
    public int getViewTypeCount() { return 1; }
    public boolean isEmpty() { return getCount() == 0; }
}
