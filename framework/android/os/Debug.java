package android.os;

/**
 * android.os.Debug.
 *
 * The memory figures are read from the host rather than hardcoded. Apps poll these
 * to decide when to drop caches or to report their own footprint in telemetry, and
 * fixed numbers make the app believe its usage never changes — so it never releases
 * anything, right up to being killed.
 */
public final class Debug {

    /** Resident footprint of this process, in bytes. */
    private static native long nativeProcessResidentMemory();

    /** Bytes this process may still allocate. */
    private static native long nativeAvailableMemory();

    /**
     * Per-process memory breakdown, in kilobytes.
     *
     * KuDroid runs one process with no split between a managed and a native heap, so
     * everything is reported as native PSS and the Dalvik share is zero. Inventing a
     * split would be a made-up number in a field apps sum up and act on; the total
     * is what they actually use.
     */
    public static class MemoryInfo implements Parcelable {
        public int dalvikPss;
        public int dalvikPrivateDirty;
        public int dalvikSharedDirty;
        public int nativePss;
        public int nativePrivateDirty;
        public int nativeSharedDirty;
        public int otherPss;
        public int otherPrivateDirty;
        public int otherSharedDirty;

        public MemoryInfo() {
            refresh(this);
        }

        static void refresh(MemoryInfo info) {
            final int residentKb = (int) (nativeProcessResidentMemory() / 1024L);
            info.dalvikPss = 0;
            info.dalvikPrivateDirty = 0;
            info.dalvikSharedDirty = 0;
            info.nativePss = residentKb;
            info.nativePrivateDirty = residentKb;
            info.nativeSharedDirty = 0;
            info.otherPss = 0;
            info.otherPrivateDirty = 0;
            info.otherSharedDirty = 0;
        }

        public int getTotalPss() { return dalvikPss + nativePss + otherPss; }

        public int getTotalPrivateDirty() {
            return dalvikPrivateDirty + nativePrivateDirty + otherPrivateDirty;
        }

        public int getTotalSharedDirty() {
            return dalvikSharedDirty + nativeSharedDirty + otherSharedDirty;
        }

        public int getTotalPrivateClean() { return 0; }

        public int getTotalSharedClean() { return 0; }

        public int getTotalSwappablePss() { return 0; }

        /**
         * Named metric lookup.
         *
         * Returns null for anything not known, which is what the platform does and
         * what callers branch on.
         */
        public String getMemoryStat(String statName) {
            if (statName == null) return null;
            if (statName.equals("summary.native-heap")) return String.valueOf(nativePss);
            if (statName.equals("summary.java-heap")) return String.valueOf(dalvikPss);
            if (statName.equals("summary.total-pss")) return String.valueOf(getTotalPss());
            if (statName.equals("summary.private-dirty")) {
                return String.valueOf(getTotalPrivateDirty());
            }
            if (statName.equals("summary.graphics")) return "0";
            if (statName.equals("summary.stack")) return "0";
            if (statName.equals("summary.code")) return "0";
            if (statName.equals("summary.system")) return "0";
            if (statName.equals("summary.total-swap")) return "0";
            return null;
        }

        public int describeContents() { return 0; }
        public void writeToParcel(Parcel dest, int flags) {}
    }

    private Debug() {}

    public static boolean isDebuggerConnected() { return false; }

    public static boolean waitingForDebugger() { return false; }

    public static void waitForDebugger() {}

    /** Native heap size: the process footprint plus what it may still take. */
    public static long getNativeHeapSize() {
        return nativeProcessResidentMemory() + nativeAvailableMemory();
    }

    public static long getNativeHeapAllocatedSize() {
        return nativeProcessResidentMemory();
    }

    public static long getNativeHeapFreeSize() {
        return nativeAvailableMemory();
    }

    public static void getMemoryInfo(MemoryInfo memoryInfo) {
        if (memoryInfo == null) return;
        MemoryInfo.refresh(memoryInfo);
    }

    public static int getGlobalAllocCount() { return 0; }

    public static int getGlobalAllocSize() { return 0; }

    public static void startAllocCounting() {}

    public static void stopAllocCounting() {}

    public static void resetAllCounts() {}

    public static long getPss() {
        return nativeProcessResidentMemory() / 1024L;
    }

    public static int getBinderSentTransactions() { return 0; }

    public static int getBinderReceivedTransactions() { return 0; }

    public static int getLoadedClassCount() { return 0; }

    public static void dumpHprofData(String fileName) {}
}
