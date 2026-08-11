package android.os;

/**
 * Minimal android.os.MessageQueue implementation.
 *
 * A simple FIFO of Messages. For KuDroid's minimal framework, messages are
 * processed synchronously in order (no timing/barriers).
 */
public final class MessageQueue {
    private Message mMessages; // head of the queue
    private boolean mQuitting = false;

    MessageQueue() {}

    /**
     * Enqueue a message. Returns true on success.
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
            return true;
        }
    }

    /**
     * Return the next message, or null if the queue is quitting.
     */
    Message next() {
        synchronized (this) {
            if (mQuitting) {
                return null;
            }
            Message msg = mMessages;
            if (msg != null) {
                mMessages = msg.next;
                msg.next = null;
            }
            return msg;
        }
    }

    /**
     * Quit the queue.
     */
    void quit() {
        synchronized (this) {
            mQuitting = true;
            mMessages = null;
        }
    }
}
