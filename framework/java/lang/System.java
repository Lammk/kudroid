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

    /**
     * Platform file name for a bare library name: "minecraftpe" to
     * "libminecraftpe.so".
     *
     * Always the Android form, never the host's. KuDroid runs guest code that was
     * built for Android, so a caller that maps a name and then opens the result must
     * get "lib*.so" even though the process itself is a Mach-O one. Code that reaches
     * this is usually about to call findLibrary() or log what it is looking for —
     * AGDK's GameActivity does both on the way to loading its renderer.
     */
    public static String mapLibraryName(String libname) {
        if (libname == null) throw new NullPointerException("libname == null");
        return "lib" + libname + ".so";
    }

    public static void setOut(java.io.PrintStream stream) {
    }

    public static void setErr(java.io.PrintStream stream) {
    }
}
