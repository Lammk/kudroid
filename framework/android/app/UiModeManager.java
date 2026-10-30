package android.app;

import android.content.res.Configuration;

/**
 * Android UiModeManager implementation.
 */
public class UiModeManager {
    public static final int MODE_NIGHT_AUTO = 0;
    public static final int MODE_NIGHT_NO = 1;
    public static final int MODE_NIGHT_YES = 2;
    public static final int MODE_NIGHT_CUSTOM = 3;

    public static final String ACTION_ENTER_CAR_MODE = "android.app.action.ENTER_CAR_MODE";
    public static final String ACTION_EXIT_CAR_MODE = "android.app.action.EXIT_CAR_MODE";
    public static final String ACTION_ENTER_DESK_MODE = "android.app.action.ENTER_DESK_MODE";
    public static final String ACTION_EXIT_DESK_MODE = "android.app.action.EXIT_DESK_MODE";

    public static final int ENABLE_CAR_MODE_GO_CAR_HOME = 0x0001;
    public static final int ENABLE_CAR_MODE_ALLOW_SLEEP = 0x0002;
    public static final int DISABLE_CAR_MODE_GO_HOME = 0x0001;

    private int mNightMode = -1;

    public int getCurrentModeType() {
        return Configuration.UI_MODE_TYPE_NORMAL;
    }

    public int getNightMode() {
        if (mNightMode >= 0) return mNightMode;
        int mode = android.content.res.Resources.getSystem().getConfiguration().uiMode;
        return (mode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES
                ? MODE_NIGHT_YES : MODE_NIGHT_NO;
    }

    public void setNightMode(int mode) {
        mNightMode = mode;
    }

    public void setApplicationNightMode(int mode) {
        mNightMode = mode;
    }

    public void enableCarMode(int flags) {}
    public void disableCarMode(int flags) {}
    public float getContrast() {
        return 0.0f;
    }
}
