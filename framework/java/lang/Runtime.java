package java.lang;

public class Runtime {

    private static final Runtime INSTANCE = new Runtime();

    private Runtime() {
    }

    public static Runtime getRuntime() {
        return INSTANCE;
    }

    public native void loadLibrary(String libname);

    public native void load(String pathname);

    public int availableProcessors() {
        return 4;
    }

    public long totalMemory() {
        return 256L * 1024 * 1024;
    }

    public long freeMemory() {
        return 128L * 1024 * 1024;
    }

    public long maxMemory() {
        return 512L * 1024 * 1024;
    }

    public void gc() {
    }

    public void exit(int status) {
        System.exit(status);
    }

    public void halt(int status) {
        System.exit(status);
    }

    public void addShutdownHook(Thread hook) {
    }

    public boolean removeShutdownHook(Thread hook) {
        return false;
    }
}
