package android.os;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;

public abstract class FileObserver {
    public static final int ACCESS = 0x001, MODIFY = 0x002, ATTRIB = 0x004;
    public static final int CLOSE_WRITE = 0x008, CLOSE_NOWRITE = 0x010, OPEN = 0x020;
    public static final int MOVED_FROM = 0x040, MOVED_TO = 0x080, CREATE = 0x100, DELETE = 0x200;
    public static final int DELETE_SELF = 0x400, MOVE_SELF = 0x800, ALL_EVENTS = 0xfff;
    private final List<File> files;
    private final int mask;
    private ArrayList<Watcher> watchers;

    public FileObserver(String path) { this(new File(path), ALL_EVENTS); }
    public FileObserver(String path, int mask) { this(new File(path), mask); }
    public FileObserver(File file) { this(file, ALL_EVENTS); }
    public FileObserver(File file, int mask) { this(java.util.Arrays.asList(file), mask); }
    public FileObserver(List<File> files) { this(files, ALL_EVENTS); }
    public FileObserver(List<File> files, int mask) {
        if (files == null) throw new NullPointerException();
        this.files = new ArrayList<File>(files);
        for (File file : this.files) if (file == null) throw new NullPointerException();
        this.mask = mask;
    }
    public synchronized void startWatching() {
        if (watchers != null) return;
        watchers = new ArrayList<Watcher>();
        for (File file : files) {
            long handle = nativeStart(file.getAbsolutePath(), mask);
            if (handle == 0) continue;
            Watcher watcher = new Watcher(this, handle);
            watchers.add(watcher);
            try { watcher.start(); } catch (RuntimeException e) {
                stopWatching(); throw e;
            } catch (Error e) { stopWatching(); throw e; }
        }
    }
    public synchronized void stopWatching() {
        if (watchers == null) return;
        for (Watcher watcher : watchers) watcher.stopWatch();
        watchers = null;
    }
    protected void finalize() { stopWatching(); }
    public abstract void onEvent(int event, String path);

    private static final class Watcher extends Thread {
        private final WeakReference<FileObserver> observer;
        private final long handle;
        private volatile boolean stopped;
        Watcher(FileObserver observer, long handle) {
            super("FileObserver");
            this.observer = new WeakReference<FileObserver>(observer);
            this.handle = handle;
            setDaemon(true);
        }
        void stopWatch() { stopped = true; nativeStop(handle); }
        public void run() {
            int[] event = new int[1];
            try {
                while (!stopped) {
                    event[0] = 0;
                    String path = nativePoll(handle, event);
                    if (stopped) break;
                    FileObserver target = observer.get();
                    if (target == null) break;
                    if (event[0] != 0) {
                        try { target.onEvent(event[0], path); }
                        catch (Throwable e) { android.util.Log.e("FileObserver", "Unhandled observer callback", e); }
                    }
                }
            } finally { stopWatch(); }
        }
    }
    private static native long nativeStart(String path, int mask);
    private static native String nativePoll(long handle, int[] event);
    private static native void nativeStop(long handle);
}
