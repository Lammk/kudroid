package android.os;

public final class PowerManager {
    public static final int PARTIAL_WAKE_LOCK = 1;
    public static final int SCREEN_DIM_WAKE_LOCK = 6;
    public static final int SCREEN_BRIGHT_WAKE_LOCK = 10;
    public static final int FULL_WAKE_LOCK = 26;
    public static final int ON_AFTER_RELEASE = 536870912;

    public final class WakeLock {
        private boolean held = false;
        WakeLock(int flags, String tag) {}
        public void acquire() { held = true; }
        public void acquire(long timeout) { held = true; }
        public void release() { held = false; }
        public void release(int flags) { held = false; }
        public boolean isHeld() { return held; }
        public void setReferenceCounted(boolean value) {}
    }

    public WakeLock newWakeLock(int levelAndFlags, String tag) {
        return new WakeLock(levelAndFlags, tag);
    }
    public boolean isInteractive() { return true; }
    public boolean isScreenOn() { return true; }
    public boolean isPowerSaveMode() { return false; }
}
