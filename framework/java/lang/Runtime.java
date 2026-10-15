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

    public native int availableProcessors();

    /**
     * Heap figures, read from the host device.
     *
     * These were fixed at 256/128/512 MB. Apps divide by maxMemory() to size caches
     * and bitmap pools, and compare freeMemory() against what they are about to
     * allocate, so a constant either invites an allocation the device cannot back —
     * and the process is killed — or holds the app to a fraction of what it could
     * use. KuART has no separate managed heap, so the process budget IS the heap
     * budget.
     */
    public native long maxMemory();

    public native long totalMemory();

    public native long freeMemory();

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
