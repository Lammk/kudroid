package android.app;

import android.content.pm.ConfigurationInfo;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * android.app.ActivityManager.
 *
 * Was an empty generated stub, which meant every nested type it declares resolved
 * to an auto-stub. That is not survivable: an app doing
 * {@code new ActivityManager.MemoryInfo()} gets NoClassDefFoundError, and reading
 * available memory is routine in games and in any app that adapts its cache size,
 * so it happens during startup.
 *
 * The figures come from the host device via native calls, not from constants. Apps
 * size caches, texture atlases and world chunks from them, so a fixed number is
 * either above what the device can give — and the app is killed mid-load — or below
 * it, and the app runs degraded on hardware that could do better.
 */
public class ActivityManager {

    public static final int MOVE_TASK_WITH_HOME = 0x00000001;
    public static final int MOVE_TASK_NO_USER_ACTION = 0x00000002;

    /** Physical RAM of the device. */
    private static native long nativeTotalMemory();

    /**
     * Memory this process may still use.
     *
     * Not the system-wide figure: iOS enforces a per-process limit well below total
     * RAM, so an app sizing itself from system-available memory allocates its way
     * into being killed.
     */
    private static native long nativeAvailableMemory();

    /** System-wide reclaimable memory, which is what MemoryInfo.availMem means. */
    private static native long nativeSystemAvailableMemory();

    private static native boolean nativeIsLowMemory();

    private static native int nativeMemoryClass();

    private static native int nativeLargeMemoryClass();

    public ActivityManager() {}

    /**
     * Device memory state.
     *
     * Callers pass an instance to getMemoryInfo() and read the fields, so the class
     * must be constructible and the fields must carry their exact Android names.
     */
    public static class MemoryInfo implements android.os.Parcelable {
        public long availMem;
        public long totalMem;
        public long threshold;
        public boolean lowMemory;
        public long hiddenAppThreshold;
        public long secondaryServerThreshold;
        public long visibleAppThreshold;
        public long foregroundAppThreshold;

        /**
         * Populated at construction from the host.
         *
         * Apps commonly construct one and read the fields without calling
         * getMemoryInfo() first — zeroes there read as "no memory left".
         */
        public MemoryInfo() {
            refresh(this);
        }

        static void refresh(MemoryInfo info) {
            info.totalMem = nativeTotalMemory();
            info.availMem = nativeSystemAvailableMemory();
            info.lowMemory = nativeIsLowMemory();
            // Android's threshold is the level at which it starts killing
            // background processes; a small fraction of total RAM.
            info.threshold = info.totalMem / 32;
            info.hiddenAppThreshold = info.threshold;
            info.secondaryServerThreshold = info.threshold;
            info.visibleAppThreshold = info.threshold;
            info.foregroundAppThreshold = info.threshold;
        }

        public int describeContents() { return 0; }
        public void writeToParcel(android.os.Parcel dest, int flags) {}
    }

    /** One running process. Apps scan this list to find themselves. */
    public static class RunningAppProcessInfo implements android.os.Parcelable {
        public static final int IMPORTANCE_FOREGROUND = 100;
        public static final int IMPORTANCE_FOREGROUND_SERVICE = 125;
        public static final int IMPORTANCE_VISIBLE = 200;
        public static final int IMPORTANCE_SERVICE = 300;
        public static final int IMPORTANCE_CACHED = 400;
        public static final int IMPORTANCE_GONE = 1000;

        public String processName;
        public int pid;
        public int uid;
        public String[] pkgList;
        public int importance;
        public int lru;
        public int importanceReasonCode;

        public RunningAppProcessInfo() {
            importance = IMPORTANCE_FOREGROUND;
        }

        public RunningAppProcessInfo(String name, int pid, String[] arr) {
            this.processName = name;
            this.pid = pid;
            this.pkgList = arr;
            this.importance = IMPORTANCE_FOREGROUND;
        }

        public int describeContents() { return 0; }
        public void writeToParcel(android.os.Parcel dest, int flags) {}
    }

    public static class RunningServiceInfo implements android.os.Parcelable {
        public String process;
        public int pid;
        public int uid;
        public boolean foreground;

        public RunningServiceInfo() {}
        public int describeContents() { return 0; }
        public void writeToParcel(android.os.Parcel dest, int flags) {}
    }

    public static class RunningTaskInfo implements android.os.Parcelable {
        public int id;
        public int numActivities;
        public CharSequence description;

        public RunningTaskInfo() {}
        public int describeContents() { return 0; }
        public void writeToParcel(android.os.Parcel dest, int flags) {}
    }

    public static class AppTask {
        public void finishAndRemoveTask() {}
        public RunningTaskInfo getTaskInfo() { return new RunningTaskInfo(); }
        public void moveToFront() {}
    }

    public void getMemoryInfo(MemoryInfo outInfo) {
        if (outInfo == null) return;
        MemoryInfo.refresh(outInfo);
    }

    /**
     * Per-app heap limit in megabytes.
     *
     * Apps size their caches from this, so it is derived from the device's real
     * per-process budget rather than a constant.
     */
    public int getMemoryClass() { return nativeMemoryClass(); }

    public int getLargeMemoryClass() { return nativeLargeMemoryClass(); }

    public boolean isLowRamDevice() {
        // Android's own cutoff for the low-RAM device profile is 1 GB.
        return nativeTotalMemory() < (1024L * 1024 * 1024);
    }

    public static boolean isUserAMonkey() { return false; }

    public static boolean isRunningInTestHarness() { return false; }

    public int getLauncherLargeIconDensity() { return 480; }

    public int getLauncherLargeIconSize() { return 96; }

    public List<RunningAppProcessInfo> getRunningAppProcesses() {
        final List<RunningAppProcessInfo> out = new ArrayList<RunningAppProcessInfo>();
        final RunningAppProcessInfo self = new RunningAppProcessInfo();
        self.processName = ActivityThread.getPackageName();
        self.pid = android.os.Process.myPid();
        self.uid = android.os.Process.myUid();
        self.pkgList = new String[] { self.processName };
        self.importance = RunningAppProcessInfo.IMPORTANCE_FOREGROUND;
        out.add(self);
        return out;
    }

    public List<RunningServiceInfo> getRunningServices(int maxNum) {
        return new ArrayList<RunningServiceInfo>();
    }

    public List<RunningTaskInfo> getRunningTasks(int maxNum) {
        return new ArrayList<RunningTaskInfo>();
    }

    public List<AppTask> getAppTasks() {
        return new ArrayList<AppTask>();
    }

    /**
     * Why the process died last time.
     *
     * KuDroid keeps no history across launches, so the list is empty — which is also
     * what Android returns on a first run, a case callers already handle.
     */
    public List<ApplicationExitInfo> getHistoricalProcessExitReasons(String packageName,
            int pid, int maxNum) {
        return new ArrayList<ApplicationExitInfo>();
    }

    public void setProcessStateSummary(byte[] state) {}

    public ConfigurationInfo getDeviceConfigurationInfo() {
        return new ConfigurationInfo();
    }

    public void killBackgroundProcesses(String packageName) {}

    public boolean clearApplicationUserData() { return false; }

    public void addAppTask(Activity activity, android.content.Intent intent,
                           android.app.ActivityManager.TaskDescription description,
                           android.graphics.Bitmap thumbnail) {}

    public static class TaskDescription implements android.os.Parcelable {
        private final String mLabel;

        public TaskDescription() { this(null); }
        public TaskDescription(String label) { mLabel = label; }
        public TaskDescription(String label, android.graphics.Bitmap icon) { mLabel = label; }
        public TaskDescription(String label, android.graphics.Bitmap icon, int colorPrimary) {
            mLabel = label;
        }

        public String getLabel() { return mLabel; }
        public int describeContents() { return 0; }
        public void writeToParcel(android.os.Parcel dest, int flags) {}
    }
}
