package android.os;

/**
 * triển khai android.os.message tối thiểu.
 *
 * đại diện cho một thông báo được gửi đến một handler. đối với khuôn khổ tối thiểu của kudroid,
 * chúng tôi giữ nó đơn giản: một handler đích, một đối số int, một đối tượng obj,
 * và một mã what.
 */
public final class Message {
    /** mã thông báo do người dùng xác định. */
    public int what;
    /** đối số do người dùng xác định đầu tiên. */
    public int arg1;
    /** đối số do người dùng xác định thứ hai. */
    public int arg2;
    /** đối tượng tùy ý để gửi đến đích. */
    public Object obj;
    /** handler sẽ xử lý thông báo này. */
    public Handler target;

    private static final Object sPoolSync = new Object();
    private static Message sPool;
    private static int sPoolSize = 0;
    private static final int MAX_POOL_SIZE = 50;

    Message next; // package-private so MessageQueue can access it

    private Message() {}

    /**
     * lấy một message từ nhóm chung.
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
     * lấy một message với một handler đích.
     */
    public static Message obtain(Handler h) {
        Message m = obtain();
        m.target = h;
        return m;
    }

    /**
     * lấy một message với một handler đích và mã what.
     */
    public static Message obtain(Handler h, int what) {
        Message m = obtain();
        m.target = h;
        m.what = what;
        return m;
    }

    /**
     * trả lại message này cho nhóm.
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
     * trả về một biểu diễn chuỗi của thông báo này.
     */
    @Override
    public String toString() {
        return "Message{what=" + what + ", arg1=" + arg1 + ", arg2=" + arg2 + "}";
    }
}
