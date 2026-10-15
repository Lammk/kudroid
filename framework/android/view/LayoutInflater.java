package android.view;

import android.content.Context;

public class LayoutInflater {
    private final Context mContext;

    public LayoutInflater() { mContext = null; }
    public LayoutInflater(Context context) { mContext = context; }
    public static LayoutInflater from(Context context) { return new LayoutInflater(context); }
    public View inflate(int resource, ViewGroup root) { return null; }
    public View inflate(int resource, ViewGroup root, boolean attachToRoot) { return null; }
    public Context getContext() { return mContext; }
}
