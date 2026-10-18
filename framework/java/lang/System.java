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

    private static native String getProperty0(String key);

    private static java.util.Properties properties;

    public static String getProperty(String key) {
        checkPropertyKey(key);
        return getProperties().getProperty(key);
    }

    public static String setProperty(String key, String value) {
        checkPropertyKey(key);
        if (value == null) throw new NullPointerException("value == null");
        return (String) getProperties().setProperty(key, value);
    }

    public static String clearProperty(String key) {
        checkPropertyKey(key);
        return (String) getProperties().remove(key);
    }

    private static void checkPropertyKey(String key) {
        if (key == null) throw new NullPointerException("key == null");
        if (key.length() == 0) throw new IllegalArgumentException("key is empty");
    }

    public static native String getenv(String name);

    public static native void exit(int status);

    // Share mutable guest properties, seeded once from native defaults.
    public static synchronized java.util.Properties getProperties() {
        if (properties != null) return properties;
        java.util.Properties p = new java.util.Properties();
        String[] keys = {
            "line.separator", "file.separator", "path.separator",
            "java.io.tmpdir", "user.dir", "user.home", "java.vm.name",
            "java.vm.vendor", "java.vendor", "java.specification.name",
            "java.vm.specification.name", "java.vm.version", "java.version",
            "java.specification.version", "java.runtime.name",
            "java.runtime.version", "os.name", "os.arch", "os.version",
            "file.encoding",
        };
        for (int i = 0; i < keys.length; i++) {
            String v = getProperty0(keys[i]);
            if (v != null) {
                p.setProperty(keys[i], v);
            }
        }
        properties = p;
        return properties;
    }

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
