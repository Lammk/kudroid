package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import java.util.List;
import java.util.Arrays;
import java.util.ArrayList;

public class ArrayAdapter<T> extends BaseAdapter {
    private final Context mContext;
    private final List<T> mObjects;

    public ArrayAdapter(Context context, int resource, T[] objects) {
        this(context, resource, new ArrayList<T>(Arrays.asList(objects)));
    }
    public ArrayAdapter(Context context, int resource, List<T> objects) {
        mContext = context;
        mObjects = objects;
    }
    public int getCount() { return mObjects.size(); }
    public T getItem(int position) { return mObjects.get(position); }
    public long getItemId(int position) { return position; }
    public View getView(int position, View convertView, ViewGroup parent) {
        TextView text = (convertView instanceof TextView) ? (TextView) convertView : new TextView(mContext);
        T item = getItem(position);
        text.setText(item != null ? item.toString() : "");
        return text;
    }
}
