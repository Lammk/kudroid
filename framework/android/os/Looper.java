package android.os;

/**
 * Minimal android.os.Looper implementation.
 *
 * A Looper runs a message loop on a thread. For KuDroid's purposes (native
 * games that only touch Java briefly at startup), we provide a simple
 * main-thread looper that processes messages synchronously.
 */
public final class Looper {
    private static final Looper sMainLooper = new Looper();
    private static boolean sMainLooperPrepared = false;

    private final MessageQueue mQueue;

    private Looper() {
        mQueue = new MessageQueue();
    }

    /**
     * Prepare the main looper (called once at startup).
     */
    public static void prepareMainLooper() {
        if (!sMainLooperPrepared) {
            sMainLooperPrepared = true;
        }
    }

    /**
     * Return the main looper for the current thread.
     */
    public static Looper getMainLooper() {
        return sMainLooper;
    }

    /**
     * Return the looper for the current thread (main looper for now).
     */
    public static Looper myLooper() {
        return sMainLooper;
    }

    /**
     * Return the message queue associated with this looper.
     */
    public MessageQueue getQueue() {
        return mQueue;
    }

    /**
     * Run the message loop. Blocks until quit() is called.
     */
    public void loop() {
        for (;;) {
            Message msg = mQueue.next();
            if (msg == null) {
                return; // queue quit
            }
            msg.target.dispatchMessage(msg);
            msg.recycle();
        }
    }

    /**
     * Quit the looper.
     */
    public void quit() {
        mQueue.quit();
    }

    /**
     * Quit the looper safely (after processing pending messages).
     */
    public void quitSafely() {
        mQueue.quit();
    }
}
