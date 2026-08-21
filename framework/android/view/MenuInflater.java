package android.view;

import android.content.Context;

/**
 * android.view.MenuInflater — dựng Menu từ XML resource.
 *
 * KuDroid chưa có resource compiler nên inflate() không thêm mục nào; app tự
 * gọi menu.add() là đường đi hoạt động được.
 */
public class MenuInflater {
    private final Context mContext;

    public MenuInflater(Context context) {
        mContext = context;
    }

    public Context getContext() {
        return mContext;
    }

    public void inflate(int menuRes, Menu menu) {
    }
}
