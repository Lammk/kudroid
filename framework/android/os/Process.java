package android.os;

public class Process {
    public static final int SYSTEM_UID = 1000;
    public static final int PHONE_UID = 1001;
    public static final int FIRST_APPLICATION_UID = 10000;
    public static final int LAST_APPLICATION_UID = 19999;
    public static final int THREAD_PRIORITY_DEFAULT = 0;
    public static final int THREAD_PRIORITY_LOWEST = 19;
    public static final int THREAD_PRIORITY_BACKGROUND = 10;
    public static final int THREAD_PRIORITY_FOREGROUND = -2;
    public static final int THREAD_PRIORITY_DISPLAY = -4;
    public static final int THREAD_PRIORITY_URGENT_DISPLAY = -8;
    public static final int THREAD_PRIORITY_AUDIO = -16;
    public static final int THREAD_PRIORITY_URGENT_AUDIO = -19;

    public static final int myPid() { return 10000; }
    public static final int myTid() { return 10000; }
    public static final int myUid() { return 10000; }
    public static final UserHandle myUserHandle() { return new UserHandle(0); }
    public static final boolean is64Bit() { return true; }
    public static final void setThreadPriority(int priority) {}
    public static final void setThreadPriority(int tid, int priority) {}
    public static final int getThreadPriority(int tid) { return 0; }
    public static final void killProcess(int pid) { System.exit(0); }
}
