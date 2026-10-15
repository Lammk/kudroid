package dalvik.system;

/**
 * The loader every Android app actually runs under.
 *
 * On Android an app's classes come from a PathClassLoader, which extends this class,
 * so {@code getClassLoader()} returns a BaseDexClassLoader and library code casts to
 * it without checking. That cast is not a nicety: AGDK's GameActivity.onCreate does
 *
 *   BaseDexClassLoader loader = (BaseDexClassLoader) getClassLoader();
 *   String path = loader.findLibrary(libname);
 *
 * to turn the {@code android.app.lib_name} meta-data into an absolute .so path, which
 * it then hands to its own native code to dlopen. With no such class in the framework
 * the cast threw ClassCastException inside onCreate and the activity never got a
 * surface — the app started, logged "Looking for library libminecraftpe.so", and
 * stopped there.
 *
 * KuART resolves classes through one DexClassLinker rather than a per-loader DEX
 * path, so the dexPath/librarySearchPath arguments are recorded but not used for
 * class lookup. {@link #findLibrary} is the part that has to be real.
 */
public class BaseDexClassLoader extends ClassLoader {

    private final String mDexPath;
    private final String mLibrarySearchPath;

    /**
     * Absolute path of a loaded guest library, or null.
     *
     * Resolved on the native side because that is the only place that knows where the
     * library actually is: the Java-visible path is the Android one
     * ({@code /data/app/<pkg>/lib/arm64-v8a}), while the file on an iOS device lives
     * under the app container. Returning the Android path would give the caller
     * something that fails to open.
     */
    private static native String findLibraryPath(String name);

    public BaseDexClassLoader(String dexPath, java.io.File optimizedDirectory,
            String librarySearchPath, ClassLoader parent) {
        super(parent);
        mDexPath = dexPath;
        mLibrarySearchPath = librarySearchPath;
    }

    public BaseDexClassLoader(String dexPath, String librarySearchPath, ClassLoader parent) {
        this(dexPath, null, librarySearchPath, parent);
    }

    public BaseDexClassLoader(String dexPath, ClassLoader parent) {
        this(dexPath, null, null, parent);
    }

    /**
     * Absolute path of the native library called {@code name}, or null if there is no
     * such library.
     *
     * `name` is the bare library name as it appears in
     * {@code android.app.lib_name} ("minecraftpe"), not the file name; the native side
     * accepts either form. Null rather than an exception on a miss, because that is
     * what callers test for — GameActivity turns it into an IllegalArgumentException
     * naming the library, which is a better message than anything thrown from here.
     */
    public String findLibrary(String name) {
        if (name == null || name.length() == 0) return null;
        return findLibraryPath(name);
    }

    public String getLdLibraryPath() {
        return mLibrarySearchPath != null ? mLibrarySearchPath : "";
    }

    @Override
    public String toString() {
        return getClass().getName() + "[DexPathList[" + mDexPath + "]]";
    }
}
