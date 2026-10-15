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
        public static final int FLAG_DIM_BEHIND = 0x00000002;
        public static final int FLAG_NOT_FOCUSABLE = 0x00000008;
        public static final int FLAG_NOT_TOUCHABLE = 0x00000010;
        public static final int FLAG_NOT_TOUCH_MODAL = 0x00000020;
        public static final int FLAG_KEEP_SCREEN_ON = 0x00000080;
        public static final int FLAG_LAYOUT_IN_SCREEN = 0x00000100;
        public static final int FLAG_LAYOUT_NO_LIMITS = 0x00000200;
        public static final int FLAG_FULLSCREEN = 0x00000400;
        public static final int FLAG_FORCE_NOT_FULLSCREEN = 0x00000800;
        public static final int FLAG_SECURE = 0x00002000;
        public static final int FLAG_HARDWARE_ACCELERATED = 0x01000000;
        public static final int FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS = 0x80000000;
        public static final int FLAG_TRANSLUCENT_STATUS = 0x04000000;
        public static final int FLAG_TRANSLUCENT_NAVIGATION = 0x08000000;

        public static final int TYPE_BASE_APPLICATION = 1;
        public static final int TYPE_APPLICATION = 2;
        public static final int TYPE_APPLICATION_PANEL = 1000;
        public static final int TYPE_SYSTEM_ALERT = 2003;
        public static final int TYPE_APPLICATION_OVERLAY = 2038;

        // Behaviour of the display cutout ("notch"). Apps set this to draw behind it,
        // which is exactly what a fullscreen game wants.
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT = 0;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES = 1;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER = 2;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS = 3;

        public static final int SOFT_INPUT_STATE_UNSPECIFIED = 0;
        public static final int SOFT_INPUT_STATE_UNCHANGED = 1;
        public static final int SOFT_INPUT_STATE_HIDDEN = 2;
        public static final int SOFT_INPUT_STATE_ALWAYS_HIDDEN = 3;
        public static final int SOFT_INPUT_STATE_VISIBLE = 4;
        public static final int SOFT_INPUT_STATE_ALWAYS_VISIBLE = 5;
        public static final int SOFT_INPUT_ADJUST_RESIZE = 0x10;
        public static final int SOFT_INPUT_ADJUST_PAN = 0x20;
        public static final int SOFT_INPUT_ADJUST_NOTHING = 0x30;

        public int type = TYPE_APPLICATION;
        public int flags;
        public int gravity;
        public int x;
        public int y;
        public float alpha = 1.0f;
        public float dimAmount = 1.0f;
        public float screenBrightness = -1.0f;

        // These were absent, so an app configuring its window — which every
        // fullscreen game does — threw NoSuchFieldError partway through setup.
        /** Pixel format; PixelFormat.UNKNOWN lets the system choose. */
        public int format = android.graphics.PixelFormat.UNKNOWN;
        /** How the window treats the display cutout. */
        public int layoutInDisplayCutoutMode = LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
        /** Soft-keyboard behaviour, from the SOFT_INPUT_* constants. */
        public int softInputMode = SOFT_INPUT_STATE_UNSPECIFIED;
        /** Owning package, used by the window manager for attribution. */
        public String packageName;
        /** Window animation style resource, 0 for the default. */
        public int windowAnimations;
        /** Token identifying the owner; KuDroid has one window so it stays null. */
        public android.os.IBinder token;
        public int rotationAnimation;
        public int preferredDisplayModeId;
        public CharSequence accessibilityTitle;

        public LayoutParams() {
            super(MATCH_PARENT, MATCH_PARENT);
        }

        public LayoutParams(int type) {
            super(MATCH_PARENT, MATCH_PARENT);
            this.type = type;
        }

        public LayoutParams(int type, int flags) {
            // Was super(0, 0): a zero-sized window, so anything laying out against
            // these parameters measured to nothing.
            super(MATCH_PARENT, MATCH_PARENT);
            this.type = type;
            this.flags = flags;
        }

        public LayoutParams(int type, int flags, int format) {
            super(MATCH_PARENT, MATCH_PARENT);
            this.type = type;
            this.flags = flags;
            this.format = format;
        }

        public LayoutParams(int width, int height, int type, int flags, int format) {
            super(width, height);
            this.type = type;
            this.flags = flags;
            this.format = format;
        }

        public LayoutParams(int type, int width, int height, int flags) {
            super(width, height);
            this.type = type;
            this.flags = flags;
        }

        public final void setTitle(CharSequence title) {}

        public final CharSequence getTitle() { return ""; }
    }
}