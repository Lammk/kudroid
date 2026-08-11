package android.view;

import android.content.Context;

/**
 * Stub android.view.WindowManager.
 *
 * Manages windows. For KuDroid's minimal framework, this is a stub.
 */
public class WindowManager {
    /** Layout param flag: not focusable. */
    public static final int FLAG_NOT_FOCUSABLE = 0x00000008;
    /** Layout param flag: not touchable. */
    public static final int FLAG_NOT_TOUCHABLE = 0x00000010;
    /** Layout param flag: keep screen on. */
    public static final int FLAG_KEEP_SCREEN_ON = 0x00000080;
    /** Layout param flag: fullscreen. */
    public static final int FLAG_FULLSCREEN = 0x00000400;

    public WindowManager() {
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
     * Layout params for a window.
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