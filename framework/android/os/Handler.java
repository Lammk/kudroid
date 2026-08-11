package android.os;

/**
 * Minimal android.os.Handler implementation.
 *
 * Allows posting Runnable/Message to a Looper. For KuDroid's minimal framework,
 * messages are dispatched synchronously when the looper runs.
 */
public class Handler {
    private final Looper mLooper;
    private final MessageQueue mQueue;
    private final Callback mCallback;

    /**
     * Callback interface for handling messages.
     */
    public interface Callback {
        boolean handleMessage(Message msg);
    }

    /**
     * Default constructor — uses the main looper.
     */
    public Handler() {
        this(Looper.getMainLooper(), null);
    }

    /**
     * Constructor with a callback.
     */
    public Handler(Callback callback) {
        this(Looper.getMainLooper(), callback);
    }

    /**
     * Constructor with an explicit looper.
     */
    public Handler(Looper looper) {
        this(looper, null);
    }

    /**
     * Constructor with an explicit looper and callback.
     */
    public Handler(Looper looper, Callback callback) {
        mLooper = looper;
        mQueue = looper.getQueue();
        mCallback = callback;
    }

    /**
     * Handle a message. Subclasses override this.
     */
    public void handleMessage(Message msg) {
    }

    /**
     * Dispatch a message to the callback or handleMessage.
     */
    public void dispatchMessage(Message msg) {
        if (mCallback != null && mCallback.handleMessage(msg)) {
            return;
        }
        handleMessage(msg);
    }

    /**
     * Post a Runnable to the queue.
     */
    public final boolean post(Runnable r) {
        return sendMessageDelayed(obtainMessage(0, r), 0);
    }

    /**
     * Post a Runnable to the queue with a delay.
     */
    public final boolean postDelayed(Runnable r, long delayMillis) {
        return sendMessageDelayed(obtainMessage(0, r), delayMillis);
    }

    /**
     * Obtain a Message with a Runnable as the obj.
     */
    public final Message obtainMessage(int what, Object obj) {
        Message m = Message.obtain();
        m.target = this;
        m.what = what;
        m.obj = obj;
        return m;
    }

    /**
     * Send a message immediately.
     */
    public final boolean sendMessage(Message msg) {
        return sendMessageDelayed(msg, 0);
    }

    /**
     * Send a message with a delay.
     */
    public final boolean sendMessageDelayed(Message msg, long delayMillis) {
        if (msg.target == null) {
            msg.target = this;
        }
        return mQueue.enqueueMessage(msg, System.currentTimeMillis() + delayMillis);
    }

    /**
     * Remove any pending posts of Runnable r.
     */
    public final void removeCallbacks(Runnable r) {
        // Minimal: no-op for now.
    }
}
