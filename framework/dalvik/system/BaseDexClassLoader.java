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
    private static native Object[] findResourceBacking(String name);

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
    protected java.net.URL findResource(String name) {
        if (name == null) throw new NullPointerException("name == null");
        Object[] backing = findResourceBacking(name);
        if (backing == null) return null;
        final String path = (String) backing[0];
        final long start = (Long) backing[1];
        final long length = (Long) backing[2];
        try {
            String spec = new java.net.URI("file", "", path, null).toASCIIString();
            if (start > 0) {
                String entry = new java.net.URI(null, null, "/" + name, null)
                        .getRawPath().substring(1);
                spec = "jar:" + spec + "!/" + entry;
            }
            return new java.net.URL(null, spec, new java.net.URLStreamHandler() {
                protected java.net.URLConnection openConnection(java.net.URL url) {
                    return new java.net.URLConnection(url) {
                        public void connect() { connected = true; }
                        public long getContentLengthLong() { return length; }
                        public java.io.InputStream getInputStream() throws java.io.IOException {
                            java.io.FileInputStream input = new java.io.FileInputStream(path);
                            try {
                                long skipped = 0;
                                while (skipped < start) {
                                    long count = input.skip(start - skipped);
                                    if (count <= 0) throw new java.io.EOFException(path);
                                    skipped += count;
                                }
                                connected = true;
                                return new android.content.res.AssetManager.BoundedInputStream(input, length);
                            } catch (java.io.IOException exception) {
                                input.close();
                                throw exception;
                            }
                        }
                    };
                }
            });
        } catch (java.net.URISyntaxException exception) {
            return null;
        } catch (java.net.MalformedURLException exception) {
            return null;
        }
    }

    @Override
    public String toString() {
        return getClass().getName() + "[DexPathList[" + mDexPath + "]]";
    }
}
