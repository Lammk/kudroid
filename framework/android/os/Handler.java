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
     * send a message to the callback, mCallback, or handleMessage.
     */
    public void dispatchMessage(Message msg) {
        if (msg.callback != null) {
            handleCallback(msg);
        } else {
            if (mCallback != null) {
                if (mCallback.handleMessage(msg)) {
                    return;
                }
            }
            handleMessage(msg);
        }
    }

    private static void handleCallback(Message message) {
        message.callback.run();
    }

    public final boolean post(Runnable r) {
        return sendMessageDelayed(getPostMessage(r), 0);
    }

    public final boolean postDelayed(Runnable r, long delayMillis) {
        return sendMessageDelayed(getPostMessage(r), delayMillis);
    }

    public final boolean postAtTime(Runnable r, long uptimeMillis) {
        return sendMessageAtTime(getPostMessage(r), uptimeMillis);
    }

    public final boolean postAtFrontOfQueue(Runnable r) {
        return sendMessageAtFrontOfQueue(getPostMessage(r));
    }

    private static Message getPostMessage(Runnable r) {
        Message m = Message.obtain();
        m.callback = r;
        return m;
    }

    private static Message getPostMessage(Runnable r, Object token) {
        Message m = Message.obtain();
        m.obj = token;
        m.callback = r;
        return m;
    }

    /**
     * A message already addressed to this handler.
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

    public final Message obtainMessage(int what, Object obj) {
        Message m = Message.obtain();
        m.target = this;
        m.what = what;
        m.obj = obj;
        return m;
    }

    public final boolean sendMessage(Message msg) {
        return sendMessageDelayed(msg, 0);
    }

    public final boolean sendEmptyMessage(int what) {
        return sendEmptyMessageDelayed(what, 0);
    }

    public final boolean sendEmptyMessageDelayed(int what, long delayMillis) {
        Message msg = Message.obtain();
        msg.what = what;
        return sendMessageDelayed(msg, delayMillis);
    }

    public final boolean sendEmptyMessageAtTime(int what, long uptimeMillis) {
        Message msg = Message.obtain();
        msg.what = what;
        return sendMessageAtTime(msg, uptimeMillis);
    }

    public final boolean sendMessageDelayed(Message msg, long delayMillis) {
        if (delayMillis < 0) {
            delayMillis = 0;
        }
        return sendMessageAtTime(msg, SystemClock.uptimeMillis() + delayMillis);
    }

    public boolean sendMessageAtTime(Message msg, long uptimeMillis) {
        MessageQueue queue = mQueue;
        if (queue == null) {
            return false;
        }
        return enqueueMessage(queue, msg, uptimeMillis);
    }

    public final boolean sendMessageAtFrontOfQueue(Message msg) {
        MessageQueue queue = mQueue;
        if (queue == null) {
            return false;
        }
        return enqueueMessage(queue, msg, 0);
    }

    private boolean enqueueMessage(MessageQueue queue, Message msg, long uptimeMillis) {
        msg.target = this;
        return queue.enqueueMessage(msg, uptimeMillis);
    }

    public final void removeCallbacks(Runnable r) {
        mQueue.removeCallbacks(this, r, null);
    }

    public final void removeCallbacks(Runnable r, Object token) {
        mQueue.removeCallbacks(this, r, token);
    }

    public final void removeMessages(int what) {
        mQueue.removeMessages(this, what, null);
    }

    public final void removeMessages(int what, Object object) {
        mQueue.removeMessages(this, what, object);
    }

    public final void removeCallbacksAndMessages(Object token) {
        mQueue.removeCallbacksAndMessages(this, token);
    }

    public final Looper getLooper() {
        return mLooper;
    }
}
