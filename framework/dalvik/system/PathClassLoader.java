package dalvik.system;

/**
 * The loader an app's own classes are loaded by on Android.
 *
 * Apps reach it two ways: a cast of {@code getClassLoader()}, and reflection that
 * checks {@code getClassLoader().getClass().getName()} against
 * "dalvik.system.PathClassLoader" to decide it is running on Android rather than a
 * desktop JVM. Both need the concrete class to exist, so it is not enough for
 * {@link BaseDexClassLoader} to be there alone.
 */
public class PathClassLoader extends BaseDexClassLoader {

    public PathClassLoader(String dexPath, ClassLoader parent) {
        super(dexPath, null, null, parent);
    }

    public PathClassLoader(String dexPath, String librarySearchPath, ClassLoader parent) {
        super(dexPath, null, librarySearchPath, parent);
    }
}
