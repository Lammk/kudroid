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
        // TEMP DIAGNOSTIC (ULTRAKILL main-quit): something called quit() on the
        // main queue mid-run. Log every MAIN-looper dispatch so the message that
        // precedes a KuLooperQuit names its poster. Main traffic is sparse;
        // worker loopers (UnityMain ticks) are deliberately excluded.
        final boolean traceDispatch = (me == getMainLooper());
        for (;;) {
            Message msg = queue.next();
            if (msg == null) return;
            if (traceDispatch) {
                String what;
                if (msg.callback != null) {
                    what = "callback=" + msg.callback.getClass().getName();
                } else {
                    what = "what=" + msg.what;
                }
                android.util.Log.e("KuDispatch", "main " + what
                        + " target=" + msg.target.getClass().getName());
            }
            // TEMP DIAGNOSTIC (ULTRAKILL dead Runnable proxies): name the Handler that
            // posts a proxy callback and the proxy's interfaces, so the stranded C++
            // peer behind it can be traced to its subsystem. Remove once identified.
            if (msg.callback != null
                    && java.lang.reflect.Proxy.isProxyClass(msg.callback.getClass())) {
                String ifaces = "";
                Class<?>[] arr = msg.callback.getClass().getInterfaces();
                for (int i = 0; i < arr.length; i++) {
                    if (i > 0) ifaces += ",";
                    ifaces += arr[i].getName();
                }
                android.util.Log.e("KuProxyPost", "target="
                        + msg.target.getClass().getName() + " proxyIfaces=" + ifaces);
            }
            msg.target.dispatchMessage(msg);
            msg.recycle();
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
    }
    public Thread getThread() { return mThread; }
    public MessageQueue getQueue() { return mQueue; }
}
