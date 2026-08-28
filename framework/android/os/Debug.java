package android.os;

import java.io.FileDescriptor;

public final class Debug {
    public static class MemoryInfo implements Parcelable {
        public int dalvikPss;
        public int nativePss;
        public int otherPss;
        public int totalPss;

        public MemoryInfo() {}
        public int getTotalPss() { return dalvikPss + nativePss + otherPss; }
        public int describeContents() { return 0; }
        public void writeToParcel(Parcel dest, int flags) {}
    }

    private Debug() {}
    public static boolean isDebuggerConnected() { return false; }
    public static boolean waitingForDebugger() { return false; }
    public static long getNativeHeapSize() { return Runtime.getRuntime().totalMemory(); }
    public static long getNativeHeapAllocatedSize() { return Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory(); }
    public static long getNativeHeapFreeSize() { return Runtime.getRuntime().freeMemory(); }
    public static void getMemoryInfo(MemoryInfo memoryInfo) {
        if (memoryInfo != null) {
            memoryInfo.dalvikPss = 10240;
            memoryInfo.nativePss = 20480;
            memoryInfo.otherPss = 5120;
            memoryInfo.totalPss = 35840;
        }
    }
}
