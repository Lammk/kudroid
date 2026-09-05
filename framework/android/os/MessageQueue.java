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

    /**
     * Idle callbacks. Unity registers one on the UnityMain queue at startup
     * (and Choreographer-style pumps use them for vsync-adjacent work); with
     * no IdleHandler type the registration died as NPE-on-null and took the
     * whole UnityMain thread down before the first frame.
     */
    public static interface IdleHandler {
        /**
         * Called when the queue has nothing ready to dispatch. Return false
         * to unregister, true to stay registered.
         */
        boolean queueIdle();
    }

    private final java.util.ArrayList<IdleHandler> mIdleHandlers =
            new java.util.ArrayList<IdleHandler>();

    /**
     * False for the main looper: quitting it kills the process's event pump.
     * Matches AOSP, where quit() on the main queue throws instead of silently
     * ending the session (which is exactly how a stray quit becomes a mystery
     * "session ended" with no exception anywhere).
     */
    private final boolean mQuitAllowed;
    private final String mOwnerName;

    MessageQueue() {
        this(true);
    }

    MessageQueue(boolean quitAllowed) {
        mQuitAllowed = quitAllowed;
        String name;
        try {
            name = Thread.currentThread().getName();
        } catch (Throwable t) {
            name = "?";
        }
        mOwnerName = name;
    }

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
        // TEMP DIAGNOSTIC (ULTRAKILL render stall): Unity's UnityMain looper is
        // driven by message what=2269 carrying a w0 enum. Log every post so a
        // missing tick (driver stopped) vs a lost tick (queue dropped) can be told
        // apart. Remove once the stall is understood.
        if (msg.what == 2269) {
            String obj = (msg.obj != null) ? msg.obj.getClass().getName() : "null";
            if (msg.obj != null) {
                try {
                    obj += ":" + msg.obj.toString();
                } catch (Throwable ignored) {}
            }
            android.util.Log.e("KuTick", "post what=2269 obj=" + obj
                    + " target=" + msg.target.getClass().getName());
        }
        synchronized (this) {
            if (mQuitting) {
                android.util.Log.w("KuLooperQuit", "message what=" + msg.what
                        + " dropped: queue of " + mOwnerName + " already quitting");
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
                        // Head is in the future: run idle handlers first (AOSP
                        // does), then wait out the remainder. Handlers that
                        // enqueue work wake this wait via notifyAll.
                        dispatchIdleHandlers();
                        long timeout = msg.when - SystemClock.uptimeMillis();
                        if (timeout > 0) {
                            try {
                                this.wait(timeout);
                            } catch (InterruptedException ignored) {}
                        }
                    } else {
                        // Message is ready to dispatch
                        mMessages = msg.next;
                        msg.next = null;
                        msg.inUse = false;
                        return msg;
                    }
                } else {
                    // Idle: run idle handlers, then wait unbounded for
                    // notification. Unlike AOSP (which re-polls immediately
                    // and can spin on a staying-registered handler), one pass
                    // per wake keeps CPU bounded; any enqueue wakes us.
                    dispatchIdleHandlers();
                    try {
                        this.wait();
                    } catch (InterruptedException ignored) {}
                }
            }
        }
    }

    public void addIdleHandler(IdleHandler handler) {
        if (handler == null) {
            throw new NullPointerException("IdleHandler must not be null");
        }
        synchronized (this) {
            mIdleHandlers.add(handler);
        }
    }

    public void removeIdleHandler(IdleHandler handler) {
        synchronized (this) {
            mIdleHandlers.remove(handler);
        }
    }

    /**
     * Runs registered idle handlers. Called from next() when the queue has no
     * message ready (empty, or head still in the future). A handler returning
     * false is unregistered, matching AOSP. Runs under the queue monitor like
     * AOSP so add/remove cannot interleave mid-dispatch.
     */
    private void dispatchIdleHandlers() {
        int count = mIdleHandlers.size();
        for (int i = 0; i < count; i++) {
            IdleHandler handler = mIdleHandlers.get(i);
            boolean keep = false;
            try {
                keep = handler.queueIdle();
            } catch (Throwable ignored) {}
            if (!keep) {
                mIdleHandlers.remove(handler);
                i--;
                count--;
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
     *
     * Like AOSP, quitting the MAIN queue throws instead of silently returning
     * null from next() forever: a stray quit (game, plugin, Swappy) then names
     * itself in the exception instead of ending the session with no trace.
     */
    void quit() {
        String caller;
        try {
            caller = Thread.currentThread().getName();
        } catch (Throwable t) {
            caller = "?";
        }
        android.util.Log.e("KuLooperQuit", "quit() queue of " + mOwnerName
                + " from thread " + caller
                + (mQuitAllowed ? " (allowed)" : " (MAIN: throwing)"));
        if (!mQuitAllowed) {
            throw new IllegalStateException("Main thread not allowed to quit.");
        }
        quitInternal();
    }

    /**
     * Unconditional quit for KuDroid's own teardown (activity destroy on the way
     * out). Never called by app code; the guard above stays total for those.
     */
    void quitInternal() {
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
