package java.lang;

public final class System {

    public static final java.io.PrintStream out = new java.io.PrintStream(1);
    public static final java.io.PrintStream err = new java.io.PrintStream(2);
    public static final java.io.InputStream in = new java.io.ByteArrayInputStream(new byte[0]);

    private System() {
    }

    public static native long currentTimeMillis();

    public static native long nanoTime();

    public static native void arraycopy(Object src, int srcPos, Object dest, int destPos,
            int length);

    public static native int identityHashCode(Object o);

    public static native String getProperty(String key);

    public static native String getenv(String name);

    public static native void exit(int status);

    public static void gc() {
    }

    public static void runFinalization() {
    }

    public static String getProperty(String key, String defaultValue) {
        String v = getProperty(key);
        return v == null ? defaultValue : v;
    }

    public static String lineSeparator() {
        return "\n";
    }

    public static void loadLibrary(String libname) {
        Runtime.getRuntime().loadLibrary(libname);
    }

    public static void load(String pathname) {
        Runtime.getRuntime().load(pathname);
    }

    public static void setOut(java.io.PrintStream stream) {
    }

    public static void setErr(java.io.PrintStream stream) {
    }
}
