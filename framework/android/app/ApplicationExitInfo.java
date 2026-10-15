package android.app;

/**
 * Why a previous instance of the process ended.
 *
 * KuDroid keeps no state across launches, so ActivityManager returns an empty list
 * and these values are never observed in practice. The class still has to exist and
 * be constructible: apps reference the reason constants at their class-initialiser
 * level, and a missing class there is a NoClassDefFoundError during startup rather
 * than at the point the history would be read.
 */
public final class ApplicationExitInfo implements android.os.Parcelable {

    public static final int REASON_UNKNOWN = 0;
    public static final int REASON_EXIT_SELF = 1;
    public static final int REASON_SIGNALED = 2;
    public static final int REASON_LOW_MEMORY = 3;
    public static final int REASON_CRASH = 4;
    public static final int REASON_CRASH_NATIVE = 5;
    public static final int REASON_ANR = 6;
    public static final int REASON_INITIALIZATION_FAILURE = 7;
    public static final int REASON_PERMISSION_CHANGE = 8;
    public static final int REASON_EXCESSIVE_RESOURCE_USAGE = 9;
    public static final int REASON_USER_REQUESTED = 10;
    public static final int REASON_USER_STOPPED = 11;
    public static final int REASON_DEPENDENCY_DIED = 12;
    public static final int REASON_OTHER = 13;
    public static final int REASON_FREEZER = 14;
    public static final int REASON_PACKAGE_STATE_CHANGE = 15;
    public static final int REASON_PACKAGE_UPDATED = 16;

    private int mReason = REASON_UNKNOWN;
    private int mStatus;
    private int mImportance;
    private int mPid;
    private int mRealUid;
    private int mPackageUid;
    private long mPss;
    private long mRss;
    private long mTimestamp;
    private String mDescription;
    private String mProcessName;
    private byte[] mProcessStateSummary;

    public ApplicationExitInfo() {}

    public int getReason() { return mReason; }
    public int getStatus() { return mStatus; }
    public int getImportance() { return mImportance; }
    public int getPid() { return mPid; }
    public int getRealUid() { return mRealUid; }
    public int getPackageUid() { return mPackageUid; }
    public long getPss() { return mPss; }
    public long getRss() { return mRss; }
    public long getTimestamp() { return mTimestamp; }
    public String getDescription() { return mDescription; }
    public String getProcessName() { return mProcessName; }
    public byte[] getProcessStateSummary() { return mProcessStateSummary; }

    public int getDefiningUid() { return mPackageUid; }

    public java.io.InputStream getTraceInputStream() { return null; }

    public int describeContents() { return 0; }

    public void writeToParcel(android.os.Parcel dest, int flags) {}

    @Override
    public String toString() {
        return "ApplicationExitInfo{reason=" + mReason + " status=" + mStatus + "}";
    }
}
