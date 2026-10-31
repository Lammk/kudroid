package android.net;

public class TrafficStats {
    public static final int UNSUPPORTED = -1;
    private static final ThreadLocal<Integer> tags = new ThreadLocal<Integer>() {
        protected Integer initialValue() { return Integer.valueOf(-1); }
    };

    public static void setThreadStatsTag(int tag) { tags.set(Integer.valueOf(tag)); }
    public static int getThreadStatsTag() { return tags.get().intValue(); }
    public static int getAndSetThreadStatsTag(int tag) {
        int previous = getThreadStatsTag();
        setThreadStatsTag(tag);
        return previous;
    }
    public static void clearThreadStatsTag() { tags.remove(); }

    // Host traffic is not attributed to guest interfaces or UIDs.
    public static long getMobileTxPackets() { return UNSUPPORTED; }
    public static long getMobileRxPackets() { return UNSUPPORTED; }
    public static long getMobileTxBytes() { return UNSUPPORTED; }
    public static long getMobileRxBytes() { return UNSUPPORTED; }
    public static long getTotalTxPackets() { return UNSUPPORTED; }
    public static long getTotalRxPackets() { return UNSUPPORTED; }
    public static long getTotalTxBytes() { return UNSUPPORTED; }
    public static long getTotalRxBytes() { return UNSUPPORTED; }
    public static long getUidTxPackets(int uid) { return UNSUPPORTED; }
    public static long getUidRxPackets(int uid) { return UNSUPPORTED; }
    public static long getUidTxBytes(int uid) { return UNSUPPORTED; }
    public static long getUidRxBytes(int uid) { return UNSUPPORTED; }
}
