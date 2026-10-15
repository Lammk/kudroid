package android.os;

/**
 * minimal android.os.message implementation.
 *
 * represents a message sent to a handler. for kudroid minimal framework,
 * we keep it simple: one destination handler, one int argument, one obj object,
 * and a what code.
 */
public final class Message {
    /** user-defined token. */
    public int what;
    /** first user-defined argument. */
    public int arg1;
    /** second user-defined argument. */
    public int arg2;
    /** arbitrary object to send to the destination. */
    public Object obj;
    /** handler will handle this message. */
    public Handler target;
    /** optional callback runnable executed on dispatch. */
    public Runnable callback;
    /** target delivery timestamp (milliseconds). */
    public long when;

    private static final Object sPoolSync = new Object();
    private static Message sPool;
    private static int sPoolSize = 0;
    private static final int MAX_POOL_SIZE = 50;

    Message next; // package-private so MessageQueue can access it
    boolean inUse = false;

    public Message() {}

    /**
     * retrieves a message from the public pool.
     */
    public static Message obtain() {
        synchronized (sPoolSync) {
            if (sPool != null) {
                Message m = sPool;
                sPool = m.next;
                m.next = null;
                m.inUse = false;
                sPoolSize--;
                return m;
            }
        }
        return new Message();
    }

    /**
     * retrieves a message with a destination handler.
     */
    public static Message obtain(Handler h) {
        Message m = obtain();
        m.target = h;
        return m;
    }

    /**
     * retrieves a message with a destination handler and callback.
     */
    public static Message obtain(Handler h, Runnable callback) {
        Message m = obtain();
        m.target = h;
        m.callback = callback;
        return m;
    }

    /**
     * retrieves a message with a destination handler and what code.
     */
    public static Message obtain(Handler h, int what) {
        Message m = obtain();
        m.target = h;
        m.what = what;
        return m;
    }

    public static Message obtain(Handler h, int what, Object obj) {
        Message m = obtain();
        m.target = h;
        m.what = what;
        m.obj = obj;
        return m;
    }

    public static Message obtain(Handler h, int what, int arg1, int arg2) {
        Message m = obtain();
        m.target = h;
        m.what = what;
        m.arg1 = arg1;
        m.arg2 = arg2;
        return m;
    }

    public static Message obtain(Handler h, int what, int arg1, int arg2, Object obj) {
        Message m = obtain();
        m.target = h;
        m.what = what;
        m.arg1 = arg1;
        m.arg2 = arg2;
        m.obj = obj;
        return m;
    }

    public static Message obtain(Message orig) {
        Message m = obtain();
        if (orig != null) {
            m.what = orig.what;
            m.arg1 = orig.arg1;
            m.arg2 = orig.arg2;
            m.obj = orig.obj;
            m.target = orig.target;
            m.callback = orig.callback;
        }
        return m;
    }

    /**
     * Send this message to the handler it was obtained from.
     *
     * Needed by all five real APKs in the corpus. The idiom is
     * {@code handler.obtainMessage(what, obj).sendToTarget()}, which is how code that builds
     * a message in one place and posts it in another avoids passing the handler along —
     * so without this the whole pattern fails at the last step, after the message is built.
     *
     * A message with no target is dropped rather than throwing. Android throws
     * NullPointerException here, but its messages always have a target because obtainMessage
     * sets it; one that does not came from Message.obtain() directly, where dropping is the
     * behaviour that keeps a logging or metrics path from taking the app down.
     */
    public void sendToTarget() {
        final Handler handler = target;
        if (handler != null) handler.sendMessage(this);
    }

    public Handler getTarget() {
        return target;
    }

    public void setTarget(Handler handler) {
        target = handler;
    }

    public int getWhat() {
        return what;
    }

    public Runnable getCallback() {
        return callback;
    }

    public long getWhen() {
        return when;
    }

    /**
     * return this message to the pool.
     */
    public void recycle() {
        this.what = 0;
        this.arg1 = 0;
        this.arg2 = 0;
        this.obj = null;
        this.target = null;
        this.callback = null;
        this.when = 0;
        this.inUse = false;
        synchronized (sPoolSync) {
            if (sPoolSize < MAX_POOL_SIZE) {
                this.next = sPool;
                sPool = this;
                sPoolSize++;
            }
        }
    }

    /**
     * returns a string representation of this message.
     */
    @Override
    public String toString() {
        return "Message{what=" + what + ", arg1=" + arg1 + ", arg2=" + arg2 + "}";
    }
}
