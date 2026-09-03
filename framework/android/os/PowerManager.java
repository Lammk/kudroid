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

    /**
     * Whether the device can be asked to hold a steady clock rate instead of boosting.
     *
     * False. iOS exposes no equivalent control, so a guest that set the window flag after a
     * true answer would believe it had pinned the clocks and size its frame budget for a
     * thermal behaviour it is not getting — which shows up as stutter under sustained load,
     * not as an error.
     */
    public boolean isSustainedPerformanceModeSupported() {
        return false;
    }

    /** Battery optimisation whitelist. True: nothing here throttles background work. */
    public boolean isIgnoringBatteryOptimizations(String packageName) {
        return true;
    }

    /**
     * Thermal status, which a game reads to decide whether to drop quality.
     *
     * NONE, because KuDroid has no way to read the host's thermal state — iOS reports
     * ProcessInfo.thermalState only to the app itself, and this process is the app. A
     * fabricated warning would make a game degrade for no reason.
     */
    public static final int THERMAL_STATUS_NONE = 0;
    public static final int THERMAL_STATUS_LIGHT = 1;
    public static final int THERMAL_STATUS_MODERATE = 2;
    public static final int THERMAL_STATUS_SEVERE = 3;
    public static final int THERMAL_STATUS_CRITICAL = 4;
    public static final int THERMAL_STATUS_EMERGENCY = 5;
    public static final int THERMAL_STATUS_SHUTDOWN = 6;

    public int getCurrentThermalStatus() {
        return THERMAL_STATUS_NONE;
    }

    public int getLocationPowerSaveMode() {
        return 0;
    }
}
