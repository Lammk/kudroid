package android.view;

import android.content.Context;

/**
 * mô phỏng android.view.windowmanager.
 *
 * quản lý các cửa sổ. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
 */
public class WindowManager {
    /** cờ thông số bố cục: không thể tập trung. */
    public static final int FLAG_NOT_FOCUSABLE = 0x00000008;
    /** cờ thông số bố cục: không thể chạm. */
    public static final int FLAG_NOT_TOUCHABLE = 0x00000010;
    /** cờ thông số bố cục: giữ màn hình luôn bật. */
    public static final int FLAG_KEEP_SCREEN_ON = 0x00000080;
    /** cờ thông số bố cục: toàn màn hình. */
    public static final int FLAG_FULLSCREEN = 0x00000400;

    public WindowManager() {
    }

    public Display getDefaultDisplay() {
        return new Display();
    }

    public void addView(View view, WindowManager.LayoutParams params) {
    }

    public void updateViewLayout(View view, WindowManager.LayoutParams params) {
    }

    public void removeView(View view) {
    }

    public void removeViewImmediate(View view) {
    }

    /**
     * thông số bố cục cho một cửa sổ.
     */
    public static class LayoutParams extends ViewGroup.LayoutParams {
        public int type;
        public int flags;
        public int gravity;
        public int x;
        public int y;
        public float alpha = 1.0f;

        public LayoutParams(int type, int flags) {
            super(0, 0);
            this.type = type;
            this.flags = flags;
        }

        public LayoutParams(int type, int width, int height, int flags) {
            super(width, height);
            this.type = type;
            this.flags = flags;
        }
    }
}