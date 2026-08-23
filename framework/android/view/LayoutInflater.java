package android.view;

import android.content.Context;

/**
 * mô phỏng android.view.layoutinflater.
 *
 * thổi phồng xml bố cục thành các view. đối với khuôn khổ tối thiểu của kudroid, đây là một
 * mô phỏng trả về rỗng.
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
     * thổi phồng một tài nguyên bố cục. hiện tại trả về rỗng.
     */
    public View inflate(int resource, ViewGroup root) {
        return null;
    }

    /**
     * thổi phồng một tài nguyên bố cục với attachtoroot.
     */
    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        return null;
    }

    /**
     * trả về ngữ cảnh.
     */
    public Context getContext() {
        return mContext;
    }

    public interface Factory2 {
    }

}