package android.os;

/**
 * minimal android.os.messagequeue implementation.
 *
 * a simple fifo of messages. for kudroid minimal framework, notifications are
 * in-order synchronous processing (no timing/barriers).
 */
public final class MessageQueue {
    private Message mMessages; // head of the queue
    private boolean mQuitting = false;

    MessageQueue() {}

    /**
     * queue a notification. Returns true if successful.
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
     * returns the next message, or null if the queue is exiting.
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
            // Sleep for 10ms if there are no new messages to yield to CPU and never exit the loop
            try {
                Thread.sleep(10);
            } catch (Throwable ignored) {}
        }
    }

    /**
     * exit queue.
     */
    void quit() {
        synchronized (this) {
            mQuitting = true;
            mMessages = null;
        }
    }
}
