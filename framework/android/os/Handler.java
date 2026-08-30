package android.os;

/**
 * minimal android.os.handler implementation.
 *
 * allow posting runnables/messages to a looper. for kudroid minimal framework,
 * messages are sent synchronously when the looper runs.
 */
public class Handler {
    private final Looper mLooper;
    private final MessageQueue mQueue;
    private final Callback mCallback;

    /**
     * callback interface for message handling.
     */
    public interface Callback {
        boolean handleMessage(Message msg);
    }

    /**
     * default constructor - uses main iterator.
     */
    public Handler() {
        this(Looper.getMainLooper(), null);
    }

    /**
     * constructor with a callback.
     */
    public Handler(Callback callback) {
        this(Looper.getMainLooper(), callback);
    }

    /**
     * constructor with an explicit iterator.
     */
    public Handler(Looper looper) {
        this(looper, null);
    }

    /**
     * constructor with an explicit iterator and callback.
     */
    public Handler(Looper looper, Callback callback) {
        mLooper = looper;
        mQueue = looper.getQueue();
        mCallback = callback;
    }

    /**
     * process a message. Subclasses override this.
     */
    public void handleMessage(Message msg) {
    }

    /**
     * send a message to the callback or handlemessage.
     */
    public void dispatchMessage(Message msg) {
        if (mCallback != null && mCallback.handleMessage(msg)) {
            return;
        }
        handleMessage(msg);
    }

    /**
     * post a runnable to the queue.
     */
    public final boolean post(Runnable r) {
        return sendMessageDelayed(obtainMessage(0, r), 0);
    }

    /**
     * post a runnable to the queue with a delay.
     */
    public final boolean postDelayed(Runnable r, long delayMillis) {
        return sendMessageDelayed(obtainMessage(0, r), delayMillis);
    }

    /**
     * A message already addressed to this handler.
     *
     * The no-argument and (what) forms exist because sendToTarget() is only usable on a
     * message that has a target, and these are where the target gets set — code that calls
     * Message.obtain() directly and then sendToTarget() has nowhere to send it.
     */
    public final Message obtainMessage() {
        Message m = Message.obtain();
        m.target = this;
        return m;
    }

    public final Message obtainMessage(int what) {
        Message m = Message.obtain();
        m.target = this;
        m.what = what;
        return m;
    }

    public final Message obtainMessage(int what, int arg1, int arg2) {
        Message m = obtainMessage(what);
        m.arg1 = arg1;
        m.arg2 = arg2;
        return m;
    }

    public final Message obtainMessage(int what, int arg1, int arg2, Object obj) {
        Message m = obtainMessage(what, arg1, arg2);
        m.obj = obj;
        return m;
    }

    /**
     * takes a message with a runnable as obj.
     */
    public final Message obtainMessage(int what, Object obj) {
        Message m = Message.obtain();
        m.target = this;
        m.what = what;
        m.obj = obj;
        return m;
    }

    /**
     * send a message instantly.
     */
    public final boolean sendMessage(Message msg) {
        return sendMessageDelayed(msg, 0);
    }

    /**
     * send a message with a delay.
     */
    public final boolean sendMessageDelayed(Message msg, long delayMillis) {
        if (msg.target == null) {
            msg.target = this;
        }
        return mQueue.enqueueMessage(msg, System.currentTimeMillis() + delayMillis);
    }

    /**
     * remove any pending posts of runnable r.
     */
    public final void removeCallbacks(Runnable r) {
        // Minimal: no-op for now.
    }
}
