package android.os;

/**
 * Android MessageQueue implementation for KuDroid.
 *
 * Maintains a linked list of Messages ordered by timestamp (when).
 * Prevents loops/cycles, supports timed wait/notify, and provides clean message cancellation.
 */
public final class MessageQueue {
    private Message mMessages; // head of the queue
    private boolean mQuitting = false;

    MessageQueue() {}

    /**
     * Enqueue a message sorted by 'when' timestamp.
     */
    boolean enqueueMessage(Message msg, long when) {
        if (msg == null) {
            return false;
        }
        if (msg.target == null) {
            throw new IllegalArgumentException("Message must have a target.");
        }
        synchronized (this) {
            if (mQuitting) {
                return false;
            }

            // Check if message is already in the queue to prevent cycles
            Message cur = mMessages;
            while (cur != null) {
                if (cur == msg) {
                    return true; // Already enqueued
                }
                cur = cur.next;
            }

            msg.inUse = true;
            msg.when = when;
            msg.next = null;

            Message p = mMessages;
            if (p == null || when == 0 || when < p.when) {
                // Insert at the head
                msg.next = p;
                mMessages = msg;
                this.notifyAll();
                return true;
            }

            // Insert in order of 'when'
            Message prev = null;
            while (p != null && p.when <= when) {
                if (p == msg) {
                    return true; // Prevent cycle
                }
                prev = p;
                p = p.next;
            }

            msg.next = p;
            if (prev != null) {
                prev.next = msg;
            }
            this.notifyAll();
            return true;
        }
    }

    /**
     * Returns the next message ready to be executed, or null if the queue is quitting.
     */
    Message next() {
        synchronized (this) {
            for (;;) {
                if (mQuitting) {
                    return null;
                }

                final long now = SystemClock.uptimeMillis();
                Message msg = mMessages;

                if (msg != null) {
                    if (now < msg.when) {
                        // Next message is in the future. Calculate wait timeout.
                        long timeout = msg.when - now;
                        try {
                            this.wait(timeout);
                        } catch (InterruptedException ignored) {}
                    } else {
                        // Message is ready to dispatch
                        mMessages = msg.next;
                        msg.next = null;
                        msg.inUse = false;
                        return msg;
                    }
                } else {
                    // No messages, wait unbounded for notification
                    try {
                        this.wait();
                    } catch (InterruptedException ignored) {}
                }
            }
        }
    }

    void removeCallbacksAndMessages(Handler h, Object token) {
        if (h == null) return;
        synchronized (this) {
            Message p = mMessages;
            while (p != null && p.target == h && (token == null || p.obj == token)) {
                Message n = p.next;
                mMessages = n;
                p.recycle();
                p = n;
            }
            while (p != null) {
                Message n = p.next;
                if (n != null) {
                    if (n.target == h && (token == null || n.obj == token)) {
                        Message nn = n.next;
                        n.recycle();
                        p.next = nn;
                        continue;
                    }
                }
                p = n;
            }
        }
    }

    void removeMessages(Handler h, int what, Object object) {
        if (h == null) return;
        synchronized (this) {
            Message p = mMessages;
            while (p != null && p.target == h && p.what == what && (object == null || p.obj == object)) {
                Message n = p.next;
                mMessages = n;
                p.recycle();
                p = n;
            }
            while (p != null) {
                Message n = p.next;
                if (n != null) {
                    if (n.target == h && n.what == what && (object == null || n.obj == object)) {
                        Message nn = n.next;
                        n.recycle();
                        p.next = nn;
                        continue;
                    }
                }
                p = n;
            }
        }
    }

    void removeCallbacks(Handler h, Runnable r, Object token) {
        if (h == null || r == null) return;
        synchronized (this) {
            Message p = mMessages;
            while (p != null && p.target == h && p.callback == r && (token == null || p.obj == token)) {
                Message n = p.next;
                mMessages = n;
                p.recycle();
                p = n;
            }
            while (p != null) {
                Message n = p.next;
                if (n != null) {
                    if (n.target == h && n.callback == r && (token == null || n.obj == token)) {
                        Message nn = n.next;
                        n.recycle();
                        p.next = nn;
                        continue;
                    }
                }
                p = n;
            }
        }
    }

    /**
     * exit queue.
     */
    void quit() {
        synchronized (this) {
            mQuitting = true;
            Message p = mMessages;
            while (p != null) {
                Message n = p.next;
                p.recycle();
                p = n;
            }
            mMessages = null;
            this.notifyAll();
        }
    }
}
