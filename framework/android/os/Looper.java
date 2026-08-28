package android.os;

public final class Looper {
    private static final ThreadLocal<Looper> sThreadLocal = new ThreadLocal<Looper>();
    private static Looper sMainLooper;
    final MessageQueue mQueue;
    final Thread mThread;

    private Looper(boolean quitAllowed) {
        mQueue = new MessageQueue();
        mThread = Thread.currentThread();
    }
    public static void prepare() {
        prepare(true);
    }
    public static void prepare(boolean quitAllowed) {
        if (sThreadLocal.get() != null) {
            throw new RuntimeException("Only one Looper may be created per thread");
        }
        sThreadLocal.set(new Looper(quitAllowed));
    }
    public static void prepareMainLooper() {
        prepare(false);
        synchronized (Looper.class) {
            if (sMainLooper != null) {
                throw new IllegalStateException("The main Looper has already been prepared.");
            }
            sMainLooper = myLooper();
        }
    }
    public static Looper getMainLooper() {
        synchronized (Looper.class) {
            return sMainLooper;
        }
    }
    public static void loop() {
        final Looper me = myLooper();
        if (me == null) throw new RuntimeException("No Looper; Looper.prepare() wasn't called on this thread.");
        final MessageQueue queue = me.mQueue;
        for (;;) {
            Message msg = queue.next();
            if (msg == null) return;
            msg.target.dispatchMessage(msg);
            msg.recycle();
        }
    }
    public static Looper myLooper() { return sThreadLocal.get(); }
    public void quit() { mQueue.quit(); }
    public void quitSafely() { mQueue.quit(); }
    public Thread getThread() { return mThread; }
    public MessageQueue getQueue() { return mQueue; }
}
