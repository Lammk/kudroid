package android.os;

/**
 * triển khai android.os.messagequeue tối thiểu.
 *
 * một fifo đơn giản của các messages. đối với khuôn khổ tối thiểu của kudroid, các thông báo được
 * xử lý đồng bộ theo thứ tự (không có định thời/rào cản).
 */
public final class MessageQueue {
    private Message mMessages; // head of the queue
    private boolean mQuitting = false;

    MessageQueue() {}

    /**
     * xếp hàng một thông báo. trả về true nếu thành công.
     */
    boolean enqueueMessage(Message msg, long when) {
        if (msg.target == null) {
            throw new IllegalArgumentException("Message must have a target.");
        }
        synchronized (this) {
            if (mQuitting) {
                return false;
            }
            msg.next = null;
            if (mMessages == null) {
                mMessages = msg;
            } else {
                // Append to the end (simple FIFO; when is ignored for now).
                Message last = mMessages;
                while (last.next != null) {
                    last = last.next;
                }
                last.next = msg;
            }
            this.notifyAll();
            return true;
        }
    }

    /**
     * trả về thông báo tiếp theo, hoặc rỗng nếu hàng đợi đang thoát.
     */
    Message next() {
        for (;;) {
            synchronized (this) {
                if (mQuitting) {
                    return null;
                }
                Message msg = mMessages;
                if (msg != null) {
                    mMessages = msg.next;
                    msg.next = null;
                    return msg;
                }
            }
            // Ngủ ngắn 10ms nếu chưa có message mới để nhường CPU và không bao giờ bị thoát vòng lặp
            try {
                Thread.sleep(10);
            } catch (Throwable ignored) {}
        }
    }

    /**
     * thoát hàng đợi.
     */
    void quit() {
        synchronized (this) {
            mQuitting = true;
            mMessages = null;
        }
    }
}
