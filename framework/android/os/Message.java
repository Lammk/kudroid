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

    private static final Object sPoolSync = new Object();
    private static Message sPool;
    private static int sPoolSize = 0;
    private static final int MAX_POOL_SIZE = 50;

    Message next; // package-private so MessageQueue can access it

    private Message() {}

    /**
     * retrieves a message from the public group.
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
     * retrieves a message with a destination handler.
     */
    public static Message obtain(Handler h) {
        Message m = obtain();
        m.target = h;
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

    /**
     * return this message to the group.
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
     * returns a string representation of this message.
     */
    @Override
    public String toString() {
        return "Message{what=" + what + ", arg1=" + arg1 + ", arg2=" + arg2 + "}";
    }
}
