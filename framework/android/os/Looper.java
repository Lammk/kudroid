package android.os;

/**
 * minimal android.os.looper implementation.
 *
 * a looper runs a message loop on a thread. for kudroid purposes (games
 * root only touches java for a while on startup), we provide a looper
 * the main thread simply processes messages synchronously.
 */
public final class Looper {
    private static final Looper sMainLooper = new Looper();
    private static boolean sMainLooperPrepared = false;

    private final MessageQueue mQueue;

    private Looper() {
        mQueue = new MessageQueue();
    }

    /**
     * prepare main looper (called once on startup).
     */
    public static void prepareMainLooper() {
        if (!sMainLooperPrepared) {
            sMainLooperPrepared = true;
        }
    }

    /**
     * returns the main looper for the current stream.
     */
    public static Looper getMainLooper() {
        return sMainLooper;
    }

    /**
     * returns the looper for the current thread (currently the main looper).
     */
    public static Looper myLooper() {
        return sMainLooper;
    }

    /**
     * returns the message queue associated with this looper.
     */
    public MessageQueue getQueue() {
        return mQueue;
    }

    /**
     * run notification loop. blocks until quit() is called.
     */
    public static void loop() {
        final Looper me = myLooper();
        if (me == null) return;
        final MessageQueue queue = me.mQueue;
        for (;;) {
            Message msg = queue.next();
            if (msg == null) {
                return; // queue quit
            }
            try {
                if (msg.target != null) {
                    msg.target.dispatchMessage(msg);
                }
            } catch (Throwable t) {
                System.err.println("[Looper] Uncaught exception in message dispatch:");
                t.printStackTrace();
            } finally {
                try {
                    msg.recycle();
                } catch (Throwable ignored) {}
            }
        }
    }

    /**
     * exit looper.
     */
    public void quit() {
        mQueue.quit();
    }

    /**
     * exit the looper safely (after processing pending messages).
     */
    public void quitSafely() {
        mQueue.quit();
    }
}
