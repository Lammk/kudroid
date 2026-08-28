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
 * The numbers reported are deliberately plausible rather than real. KuDroid runs on
 * iOS, where the host does not expose per-process PSS the way Linux does, and an
 * app that sees 0 bytes available concludes the device is out of memory and
 * degrades or refuses to run.
 */
public class ActivityManager {

    /** Reported total device RAM. */
    private static final long TOTAL_MEM = 4L * 1024 * 1024 * 1024;
    /** Reported free RAM; kept a large fraction so heuristics do not trip. */
    private static final long AVAIL_MEM = 2L * 1024 * 1024 * 1024;

    public static final int MOVE_TASK_WITH_HOME = 0x00000001;
    public static final int MOVE_TASK_NO_USER_ACTION = 0x00000002;

    public ActivityManager() {}

    /**
     * Device memory state.
     *
     * Callers pass an instance to getMemoryInfo() and read the fields, so the class
     * must be constructible and the fields must be present under their exact names.
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

        public MemoryInfo() {
            availMem = AVAIL_MEM;
            totalMem = TOTAL_MEM;
            threshold = 128L * 1024 * 1024;
            lowMemory = false;
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
        outInfo.availMem = AVAIL_MEM;
        outInfo.totalMem = TOTAL_MEM;
        outInfo.threshold = 128L * 1024 * 1024;
        outInfo.lowMemory = false;
    }

    /**
     * Per-app heap limit in megabytes.
     *
     * Apps size their caches from this. Returning 0 makes them allocate nothing;
     * these are the values a 4 GB Android device reports.
     */
    public int getMemoryClass() { return 192; }

    public int getLargeMemoryClass() { return 512; }

    public boolean isLowRamDevice() { return false; }

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
