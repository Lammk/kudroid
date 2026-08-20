package android.os;

/**
 * triển khai android.os.looper tối thiểu.
 *
 * một looper chạy một vòng lặp thông báo trên một luồng. đối với mục đích của kudroid (các trò chơi
 * gốc chỉ chạm vào java một lúc khi khởi động), chúng tôi cung cấp một looper
 * luồng chính đơn giản xử lý các thông báo một cách đồng bộ.
 */
public final class Looper {
    private static final Looper sMainLooper = new Looper();
    private static boolean sMainLooperPrepared = false;

    private final MessageQueue mQueue;

    private Looper() {
        mQueue = new MessageQueue();
    }

    /**
     * chuẩn bị looper chính (được gọi một lần khi khởi động).
     */
    public static void prepareMainLooper() {
        if (!sMainLooperPrepared) {
            sMainLooperPrepared = true;
        }
    }

    /**
     * trả về looper chính cho luồng hiện tại.
     */
    public static Looper getMainLooper() {
        return sMainLooper;
    }

    /**
     * trả về looper cho luồng hiện tại (hiện tại là looper chính).
     */
    public static Looper myLooper() {
        return sMainLooper;
    }

    /**
     * trả về hàng đợi thông báo được liên kết với looper này.
     */
    public MessageQueue getQueue() {
        return mQueue;
    }

    /**
     * chạy vòng lặp thông báo. chặn cho đến khi quit() được gọi.
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
     * thoát khỏi looper.
     */
    public void quit() {
        mQueue.quit();
    }

    /**
     * thoát khỏi looper một cách an toàn (sau khi xử lý các thông báo đang chờ xử lý).
     */
    public void quitSafely() {
        mQueue.quit();
    }
}
