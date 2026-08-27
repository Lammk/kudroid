package android.view;

import android.content.Context;

/**
 * triển khai android.view.window tối thiểu.
 *
 * đại diện cho một cửa sổ. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng
 * cung cấp quản lý cửa sổ cơ bản.
 */
public class Window {
    private final Context mContext;
    private View mDecorView;
    private int mFlags;

    public Window(Context context) {
        mContext = context;
    }

    /**
     * trả về ngữ cảnh mà cửa sổ này được tạo với.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * thiết lập view nội dung.
     */
    public void setContentView(int layoutResID) {
    }

    /**
     * thiết lập view nội dung thành một view.
     */
    public void setContentView(View view) {
        mDecorView = view;
    }

    /**
     * trả về view trang trí (decor view).
     */
    public View getDecorView() {
        return mDecorView;
    }

    private static native void setKeepScreenOnNative(boolean keepOn);

    /**
     * thiết lập cờ cửa sổ.
     */
    public void setFlags(int flags, int mask) {
        mFlags = (mFlags & ~mask) | (flags & mask);
        if ((mask & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0) {
            try {
                setKeepScreenOnNative((mFlags & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0);
            } catch (Throwable ignored) {}
        }
    }

    /**
     * thêm một cờ cửa sổ.
     */
    public void addFlags(int flags) {
        mFlags |= flags;
        if ((flags & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0) {
            try {
                setKeepScreenOnNative(true);
            } catch (Throwable ignored) {}
        }
    }

    /**
     * xóa một cờ cửa sổ.
     */
    public void clearFlags(int flags) {
        mFlags &= ~flags;
        if ((flags & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0) {
            try {
                setKeepScreenOnNative(false);
            } catch (Throwable ignored) {}
        }
    }

    /**
     * trả về các cờ cửa sổ hiện tại.
     */
    public int getFlags() {
        return mFlags;
    }

    /**
     * thiết lập nền cửa sổ.
     */
    public void setBackgroundDrawable(android.graphics.drawable.Drawable drawable) {
    }

    /**
     * thiết lập tiêu đề cửa sổ.
     */
    public void setTitle(CharSequence title) {
    }

    public interface Callback {
    }

    public interface OnFrameMetricsAvailableListener {
    }

}
