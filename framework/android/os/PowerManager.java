package android.os;

/**
 * emulate android.os.powermanager.
 *
 * is not important for application startup/rendering. Returns default value to the application
 * don't crash when they query power status.
 */
public class PowerManager {
    /** final key flag: partial. */
    public static final int PARTIAL_WAKE_LOCK = 1;
    /** wake lock flag: dimmed screen. */
    public static final int SCREEN_DIM_WAKE_LOCK = 6;
    /** wake lock flag: bright screen. */
    public static final int SCREEN_BRIGHT_WAKE_LOCK = 10;
    /** final key flag: full. */
    public static final int FULL_WAKE_LOCK = 26;

    public PowerManager() {
    }

    public boolean isScreenOn() {
        return true;
    }

    public boolean isInteractive() {
        return true;
    }

    public WakeLock newWakeLock(int flags, String tag) {
        return new WakeLock();
    }

    public void wakeUp(long time) {
    }

    public void goToSleep(long time) {
    }

    /**
     * simulate wakelock.
     */
    public static class WakeLock {
        private static native void setKeepScreenOnNative(boolean keepOn);
        private boolean mHeld = false;

        public void acquire() {
            mHeld = true;
            try {
                setKeepScreenOnNative(true);
            } catch (Throwable ignored) {}
        }

        public void acquire(long timeout) {
            acquire();
        }

        public void release() {
            mHeld = false;
            try {
                setKeepScreenOnNative(false);
            } catch (Throwable ignored) {}
        }

        public void release(int flags) {
            release();
        }

        public boolean isHeld() {
            return mHeld;
        }

        public void setReferenceCounted(boolean value) {
        }
    }
}