package java.lang;

/**
 * Thread thật do KuART tạo bằng pthread. Chỉ hỗ trợ start/join/sleep — không
 * có thread group, priority thật hay interrupt bất đồng bộ.
 */
public class Thread implements Runnable {

    public static final int MIN_PRIORITY = 1;
    public static final int NORM_PRIORITY = 5;
    public static final int MAX_PRIORITY = 10;

    public interface UncaughtExceptionHandler {
        void uncaughtException(Thread t, Throwable e);
    }

    public enum State {
        NEW, RUNNABLE, BLOCKED, WAITING, TIMED_WAITING, TERMINATED
    }

    private static UncaughtExceptionHandler defaultHandler;
    private static int threadSeq;

    private Runnable target;
    private String name;
    private int priority = NORM_PRIORITY;
    private boolean daemon;
    private boolean started;
    private boolean finished;
    private boolean interrupted;
    private UncaughtExceptionHandler handler;

    /** Con trỏ pthread_t do native giữ; 0 = chưa chạy. */
    private long nativePeer;

    public Thread() {
        this((Runnable) null, nextName());
    }

    public Thread(String name) {
        this((Runnable) null, name);
    }

    public Thread(Runnable target) {
        this(target, nextName());
    }

    public Thread(Runnable target, String name) {
        this.target = target;
        this.name = name == null ? nextName() : name;
    }

    public Thread(ThreadGroup group, Runnable target) {
        this(target, nextName());
    }

    public Thread(ThreadGroup group, Runnable target, String name) {
        this(target, name);
    }

    public Thread(ThreadGroup group, String name) {
        this((Runnable) null, name);
    }

    private static synchronized String nextName() {
        return "Thread-" + (threadSeq++);
    }

    public synchronized void start() {
        if (started) {
            throw new IllegalStateException("thread đã start");
        }
        started = true;
        nativeStart();
    }

    public void run() {
        if (target != null) {
            target.run();
        }
    }

    /** Điểm vào mà native gọi trên thread mới. */
    private void runFromNative() {
        try {
            run();
        } catch (Throwable t) {
            UncaughtExceptionHandler h = handler != null ? handler : defaultHandler;
            if (h != null) {
                h.uncaughtException(this, t);
            } else {
                t.printStackTrace();
            }
        } finally {
            synchronized (this) {
                finished = true;
                notifyAll();
            }
        }
    }

    public final String getName() {
        return name;
    }

    public final void setName(String name) {
        this.name = name;
    }

    public final int getPriority() {
        return priority;
    }

    public final void setPriority(int priority) {
        this.priority = priority;
    }

    public final boolean isDaemon() {
        return daemon;
    }

    public final void setDaemon(boolean on) {
        this.daemon = on;
    }

    public final synchronized boolean isAlive() {
        return started && !finished;
    }

    public State getState() {
        if (!started) {
            return State.NEW;
        }
        return finished ? State.TERMINATED : State.RUNNABLE;
    }

    public long getId() {
        return nativePeer;
    }

    public void interrupt() {
        interrupted = true;
    }

    public boolean isInterrupted() {
        return interrupted;
    }

    public static boolean interrupted() {
        Thread t = currentThread();
        boolean was = t.interrupted;
        t.interrupted = false;
        return was;
    }

    public final void join() throws InterruptedException {
        join(0);
    }

    public final synchronized void join(long millis) throws InterruptedException {
        if (!started) {
            return;
        }
        if (millis == 0) {
            while (!finished) {
                wait();
            }
        } else {
            long deadline = System.currentTimeMillis() + millis;
            while (!finished) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) {
                    return;
                }
                wait(remaining);
            }
        }
    }

    public void setUncaughtExceptionHandler(UncaughtExceptionHandler h) {
        this.handler = h;
    }

    public UncaughtExceptionHandler getUncaughtExceptionHandler() {
        return handler != null ? handler : defaultHandler;
    }

    public ThreadGroup getThreadGroup() {
        return null;
    }

    public StackTraceElement[] getStackTrace() {
        return new StackTraceElement[0];
    }

    public ClassLoader getContextClassLoader() {
        return ClassLoader.getSystemClassLoader();
    }

    public void setContextClassLoader(ClassLoader loader) {
    }

    public String toString() {
        return "Thread[" + name + "," + priority + "]";
    }

    public static void setDefaultUncaughtExceptionHandler(UncaughtExceptionHandler h) {
        defaultHandler = h;
    }

    public static UncaughtExceptionHandler getDefaultUncaughtExceptionHandler() {
        return defaultHandler;
    }

    public static void sleep(long millis) throws InterruptedException {
        sleep(millis, 0);
    }

    public static native void sleep(long millis, int nanos) throws InterruptedException;

    public static native Thread currentThread();

    public static native void yield();

    public static void dumpStack() {
    }

    public static int activeCount() {
        return 1;
    }

    private native void nativeStart();
}
