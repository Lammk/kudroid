package android.os;

public final class Looper {
    private static final ThreadLocal<Looper> sThreadLocal = new ThreadLocal<Looper>();
    private static Looper sMainLooper;
    final MessageQueue mQueue;
    final Thread mThread;

    private Looper(boolean quitAllowed) {
        mQueue = new MessageQueue(quitAllowed);
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
        Looper current = sThreadLocal.get();
        if (current != null) {
            if (current.mQueue.isQuitting()) {
                sThreadLocal.remove();
            } else {
                throw new RuntimeException("Only one Looper may be created per thread");
            }
        }
        synchronized (Looper.class) {
            if (sMainLooper != null) {
                if (sMainLooper.mQueue.isQuitting()) {
                    if (sThreadLocal.get() == sMainLooper) sThreadLocal.remove();
                    sMainLooper = null;
                } else {
                    throw new IllegalStateException("The main Looper has already been prepared.");
                }
            }
            prepare(false);
            sMainLooper = myLooper();
        }
        // Register the main queue as the touch wake target: injection wakes its
        // native slot, and the UI thread drains/constructs touch itself.
        sMainLooper.mQueue.setAsMainQueue();
    }
    public static Looper getMainLooper() {
        synchronized (Looper.class) {
            return sMainLooper;
        }
    }
    /**
     * Clears the main Looper retained by KuDroid's host VM between app runs.
     * ThreadLocal.remove() only affects the current (owning) guest thread.
     */
    public static void resetMainLooperForRelaunch() {
        synchronized (Looper.class) {
            sMainLooper = null;
        }
        sThreadLocal.remove();
    }
    public static void loop() {
        final Looper me = myLooper();
        if (me == null) throw new RuntimeException("No Looper; Looper.prepare() wasn't called on this thread.");
        final MessageQueue queue = me.mQueue;
        try {
            for (;;) {
                Message msg = queue.next();
                if (msg == null) return;
                msg.target.dispatchMessage(msg);
                msg.recycle();
            }
        } finally {
            queue.disposeAfterLoop();
            synchronized (Looper.class) {
                if (sMainLooper == me) sMainLooper = null;
            }
            if (sThreadLocal.get() == me) sThreadLocal.remove();
        }
    }
    public static Looper myLooper() { return sThreadLocal.get(); }
    public static MessageQueue myQueue() {
        return myLooper().getQueue();
    }
    public void quit() { mQueue.quit(); }
    public void quitSafely() { mQueue.quit(); }
    /**
     * KuDroid teardown only (activity destroy on the way out): quits even the
     * main looper. The guarded quit() above stays total for app code, exactly
     * like AOSP where the main looper can never be quit by an app.
     */
    public void quitForTeardown() {
        android.util.Log.e("KuLooperQuit", "teardown quit looper of " + mThread.getName());
        mQueue.quitInternal();
        synchronized (Looper.class) {
            if (sMainLooper == this) sMainLooper = null;
        }
        if (sThreadLocal.get() == this) sThreadLocal.remove();
    }
    public Thread getThread() { return mThread; }
    public MessageQueue getQueue() { return mQueue; }
}
