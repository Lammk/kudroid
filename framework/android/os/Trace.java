package android.os;

public final class Trace {
    public static final long TRACE_TAG_APP = 1L << 12;

    private Trace() {}
    public static void beginSection(String sectionName) {}
    public static void endSection() {}
    public static boolean isEnabled() { return false; }
    public static void traceBegin(long traceTag, String methodName) {}
    public static void traceEnd(long traceTag) {}
    public static void asyncTraceBegin(long traceTag, String methodName, int cookie) {}
    public static void asyncTraceEnd(long traceTag, String methodName, int cookie) {}
}
