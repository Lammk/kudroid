package android.view;

import android.content.Context;

/**
 * Stub android.view.LayoutInflater.
 *
 * Inflates layout XML into views. For KuDroid's minimal framework, this is a
 * stub that returns null.
 */
public class LayoutInflater {
    private final Context mContext;

    public LayoutInflater() {
        mContext = null;
    }

    public LayoutInflater(Context context) {
        mContext = context;
    }

    /**
     * Inflate a layout resource. Returns null for now.
     */
    public View inflate(int resource, ViewGroup root) {
        return null;
    }

    /**
     * Inflate a layout resource with attachToRoot.
     */
    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        return null;
    }

    /**
     * Return the context.
     */
    public Context getContext() {
        return mContext;
    }
}