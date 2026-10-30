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

    /**
     * Starting a program fails the way a missing executable does: callers already handle
     * IOException, and the host has nothing to spawn.
     */
    public Process exec(String command) throws java.io.IOException {
        if (command == null) throw new NullPointerException("command == null");
        if (command.length() == 0) throw new IllegalArgumentException("Empty command");
        return exec(command.trim().split("\\s+"));
    }

    public Process exec(String command, String[] envp) throws java.io.IOException {
        return exec(command);
    }

    public Process exec(String command, String[] envp, java.io.File dir) throws java.io.IOException {
        return exec(command);
    }

    public Process exec(String[] cmdarray) throws java.io.IOException {
        if (cmdarray == null) throw new NullPointerException("cmdarray == null");
        return new ProcessBuilder(cmdarray).start();
    }

    public Process exec(String[] cmdarray, String[] envp) throws java.io.IOException {
        return exec(cmdarray);
    }

    public Process exec(String[] cmdarray, String[] envp, java.io.File dir) throws java.io.IOException {
        return exec(cmdarray);
    }

    public void addShutdownHook(Thread hook) {
    }

    public boolean removeShutdownHook(Thread hook) {
        return false;
    }
}
