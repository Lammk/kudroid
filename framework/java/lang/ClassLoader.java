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

    public static ClassLoader getSystemClassLoader() {
        return SystemHolder.INSTANCE;
    }

    private static final class SystemHolder {
        static final ClassLoader INSTANCE = new ClassLoader();
    }
}
