package android.view;

import android.content.Context;

/**
 * emulate android.view.windowmanager.
 *
 * manage windows. for kudroid minimal framework, here is an emulation.
 */
public class WindowManager {
    /** layout parameter flag: cannot focus. */
    public static final int FLAG_NOT_FOCUSABLE = 0x00000008;
    /** layout parameter flag: untouchable. */
    public static final int FLAG_NOT_TOUCHABLE = 0x00000010;
    /** layout parameter flag: keep screen on. */
    public static final int FLAG_KEEP_SCREEN_ON = 0x00000080;
    /** layout parameter flag: fullscreen. */
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
     * layout parameters for a window.
     */
    public static class LayoutParams extends ViewGroup.LayoutParams {
        public static final int FLAG_NOT_FOCUSABLE = 0x00000008;
        public static final int FLAG_NOT_TOUCHABLE = 0x00000010;
        public static final int FLAG_KEEP_SCREEN_ON = 0x00000080;
        public static final int FLAG_FULLSCREEN = 0x00000400;

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