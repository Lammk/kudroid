package android.view;

import android.content.Context;

/**
 * simulate android.view.layoutinflater.
 *
 * inflate layout xml into views. for kudroid minimal framework, here is one
 * simulate returns null.
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
     * inflates a layout resource. currently returns empty.
     */
    public View inflate(int resource, ViewGroup root) {
        return null;
    }

    /**
     * inflate a layout resource with attachtoroot.
     */
    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        return null;
    }

    /**
     * returns context.
     */
    public Context getContext() {
        return mContext;
    }

    public interface Factory2 {
    }

}