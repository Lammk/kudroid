package android.os;

/**
 * Minimal android.os.Message implementation.
 *
 * Represents a message to be dispatched to a Handler. For KuDroid's minimal
 * framework, we keep it simple: a target Handler, an int arg, an Object obj,
 * and a what code.
 */
public final class Message {
    /** User-defined message code. */
    public int what;
    /** First user-defined argument. */
    public int arg1;
    /** Second user-defined argument. */
    public int arg2;
    /** Arbitrary object to send to the target. */
    public Object obj;
    /** The Handler that will process this message. */
    public Handler target;

    private static final Object sPoolSync = new Object();
    private static Message sPool;
    private static int sPoolSize = 0;
    private static final int MAX_POOL_SIZE = 50;

    Message next; // package-private so MessageQueue can access it

    private Message() {}

    /**
     * Obtain a Message from the global pool.
     */
    public static Message obtain() {
        synchronized (sPoolSync) {
            if (sPool != null) {
                Message m = sPool;
                sPool = m.next;
                m.next = null;
                sPoolSize--;
                return m;
            }
        }
        return new Message();
    }

    /**
     * Obtain a Message with a target Handler.
     */
    public static Message obtain(Handler h) {
        Message m = obtain();
        m.target = h;
        return m;
    }

    /**
     * Obtain a Message with a target Handler and what code.
     */
    public static Message obtain(Handler h, int what) {
        Message m = obtain();
        m.target = h;
        m.what = what;
        return m;
    }

    /**
     * Return this Message to the pool.
     */
    public void recycle() {
        this.what = 0;
        this.arg1 = 0;
        this.arg2 = 0;
        this.obj = null;
        this.target = null;
        synchronized (sPoolSync) {
            if (sPoolSize < MAX_POOL_SIZE) {
                this.next = sPool;
                sPool = this;
                sPoolSize++;
            }
        }
    }

    /**
     * Return a string representation of this message.
     */
    @Override
    public String toString() {
        return "Message{what=" + what + ", arg1=" + arg1 + ", arg2=" + arg2 + "}";
    }
}
