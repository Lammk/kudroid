package android.os;

/**
 * triển khai android.os.handler tối thiểu.
 *
 * cho phép đăng runnable/message lên một looper. đối với khuôn khổ tối thiểu của kudroid,
 * các tin nhắn được gửi đồng bộ khi looper chạy.
 */
public class Handler {
    private final Looper mLooper;
    private final MessageQueue mQueue;
    private final Callback mCallback;

    /**
     * giao diện cuộc gọi lại để xử lý tin nhắn.
     */
    public interface Callback {
        boolean handleMessage(Message msg);
    }

    /**
     * hàm tạo mặc định - sử dụng trình lặp chính.
     */
    public Handler() {
        this(Looper.getMainLooper(), null);
    }

    /**
     * hàm tạo với một cuộc gọi lại.
     */
    public Handler(Callback callback) {
        this(Looper.getMainLooper(), callback);
    }

    /**
     * hàm tạo với một trình lặp rõ ràng.
     */
    public Handler(Looper looper) {
        this(looper, null);
    }

    /**
     * hàm tạo với một trình lặp và cuộc gọi lại rõ ràng.
     */
    public Handler(Looper looper, Callback callback) {
        mLooper = looper;
        mQueue = looper.getQueue();
        mCallback = callback;
    }

    /**
     * xử lý một tin nhắn. các lớp con ghi đè điều này.
     */
    public void handleMessage(Message msg) {
    }

    /**
     * gửi một tin nhắn đến cuộc gọi lại hoặc handlemessage.
     */
    public void dispatchMessage(Message msg) {
        if (mCallback != null && mCallback.handleMessage(msg)) {
            return;
        }
        handleMessage(msg);
    }

    /**
     * đăng một runnable vào hàng đợi.
     */
    public final boolean post(Runnable r) {
        return sendMessageDelayed(obtainMessage(0, r), 0);
    }

    /**
     * đăng một runnable vào hàng đợi với một độ trễ.
     */
    public final boolean postDelayed(Runnable r, long delayMillis) {
        return sendMessageDelayed(obtainMessage(0, r), delayMillis);
    }

    /**
     * lấy một message với một runnable làm obj.
     */
    public final Message obtainMessage(int what, Object obj) {
        Message m = Message.obtain();
        m.target = this;
        m.what = what;
        m.obj = obj;
        return m;
    }

    /**
     * gửi một tin nhắn ngay lập tức.
     */
    public final boolean sendMessage(Message msg) {
        return sendMessageDelayed(msg, 0);
    }

    /**
     * gửi một tin nhắn với một độ trễ.
     */
    public final boolean sendMessageDelayed(Message msg, long delayMillis) {
        if (msg.target == null) {
            msg.target = this;
        }
        return mQueue.enqueueMessage(msg, System.currentTimeMillis() + delayMillis);
    }

    /**
     * xóa bất kỳ bài đăng nào đang chờ xử lý của runnable r.
     */
    public final void removeCallbacks(Runnable r) {
        // Minimal: no-op for now.
    }
}
