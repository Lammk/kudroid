package java.lang;

public class ClassLoader {

    private final ClassLoader parent;

    protected ClassLoader() {
        this.parent = null;
    }

    protected ClassLoader(ClassLoader parent) {
        this.parent = parent;
    }

    public ClassLoader getParent() {
        return parent;
    }

    public Class<?> loadClass(String name) throws ClassNotFoundException {
        return Class.forName(name);
    }

    protected Class<?> findClass(String name) throws ClassNotFoundException {
        return Class.forName(name);
    }

    public java.io.InputStream getResourceAsStream(String name) {
        return null;
    }

    public String findLibrary(String libname) {
        if (libname == null) return null;
        if (!libname.startsWith("lib")) libname = "lib" + libname;
        if (!libname.endsWith(".so")) libname = libname + ".so";
        return "/data/app/" + android.app.ActivityThread.getPackageName() + "/lib/arm64-v8a/" + libname;
    }

    public static ClassLoader getSystemClassLoader() {
        return SystemHolder.INSTANCE;
    }

    /**
     * The one loader in the process, created lazily.
     *
     * It is a {@link dalvik.system.PathClassLoader} rather than a bare ClassLoader
     * because that is what an app gets on Android, and app code relies on the
     * concrete type. Two patterns depend on it: a cast to BaseDexClassLoader to reach
     * {@code findLibrary()} (AGDK's GameActivity does this in onCreate to locate its
     * renderer .so), and a name comparison against "dalvik.system.PathClassLoader" to
     * detect Android rather than a desktop JVM. Returning the base class made the
     * first throw ClassCastException and the second silently take the wrong branch.
     *
     * The constructor arguments are left null: KuART resolves classes through one
     * DexClassLinker, so there is no per-loader DEX path, and PathClassLoader fills in
     * the native library directory from the runtime when it is not given one.
     */
    private static final class SystemHolder {
        static final ClassLoader INSTANCE = new dalvik.system.PathClassLoader(null, null, null);
    }
}
