package android.os;

/**
 * Stub android.os.PowerManager.
 *
 * Non-critical for app startup/rendering. Returns defaults so apps don't
 * crash when they query power state.
 */
public class PowerManager {
    /** Wake lock flag: partial. */
    public static final int PARTIAL_WAKE_LOCK = 1;
    /** Wake lock flag: screen dim. */
    public static final int SCREEN_DIM_WAKE_LOCK = 6;
    /** Wake lock flag: screen bright. */
    public static final int SCREEN_BRIGHT_WAKE_LOCK = 10;
    /** Wake lock flag: full. */
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
     * Stub WakeLock.
     */
    public static class WakeLock {
        public void acquire() {
        }

        public void acquire(long timeout) {
        }

        public void release() {
        }

        public void release(int flags) {
        }

        public boolean isHeld() {
            return false;
        }

        public void setReferenceCounted(boolean value) {
        }
    }
}