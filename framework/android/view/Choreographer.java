package android.view;

/**
 * Frame callbacks, the Java half.
 * Shares one frame source with the NDK pacer so both APIs see the same timestamps.
 */
public final class Choreographer {

    /**
     * A frame callback.
     * Time is nanoseconds on the same timeline as System.nanoTime().
     */
    public interface FrameCallback {
        void doFrame(long frameTimeNanos);
    }

    /**
     * One instance per thread, as on Android.
     */
    private static final ThreadLocal<Choreographer> sThreadInstance =
            new ThreadLocal<Choreographer>() {
                @Override
                protected Choreographer initialValue() {
                    return new Choreographer();
                }
            };

    /** Handle to the native frame queue. Zero until the first post. */
    private long nativeInstance;

    // Java-side delivery. The NDK pacer cannot reach a guest thread parked in
    // MessageQueue.next(): its wake pipe feeds an NDK looper nobody polls, so
    // every Java callback fell back to pacer-thread direct delivery with a full
    // grace interval of slip (~23fps instead of 60). Posted here instead as a
    // timestamped Message, the callback runs on this thread when due.
    private android.os.Handler mHandler;
    private final java.util.HashMap<FrameCallback, java.util.ArrayList<Runnable>>
            mPending = new java.util.HashMap<FrameCallback, java.util.ArrayList<Runnable>>();
    // Phase anchor, mirroring the pacer's last_due: re-posts from inside doFrame
    // align to the previous boundary instead of drifting a grace per frame.
    private long mLastDueNanos = 0;

    private Choreographer() {}

    /**
     * This thread's Choreographer. Never null.
     */
    public static Choreographer getInstance() {
        return sThreadInstance.get();
    }

    /** Run {@code callback} once, on the next frame. */
    public void postFrameCallback(FrameCallback callback) {
        postFrameCallbackDelayed(callback, 0L);
    }

    /**
     * Run {@code callback} on the first frame at least {@code delayMillis} from now.
     *
     * Delivered through this thread's MessageQueue (not the NDK pacer), so the
     * callback runs on the guest thread that posted it, at pacer phase.
     */
    public void postFrameCallbackDelayed(FrameCallback callback, long delayMillis) {
        if (callback == null) {
            throw new IllegalArgumentException("callback must not be null");
        }
        if (delayMillis < 0L) delayMillis = 0L;
        if (android.os.Looper.myLooper() == null) {
            // No queue on this thread: fall back to pacer-thread delivery.
            nativePostFrameCallback(callback, delayMillis);
            return;
        }
        if (mHandler == null) mHandler = new android.os.Handler();
        final long now = System.nanoTime();
        final long interval = nativeGetFrameIntervalNanos();
        long due = now + delayMillis * 1000000L + interval;
        if (delayMillis == 0L && mLastDueNanos != 0L) {
            final long boundary = mLastDueNanos + interval;
            if (boundary > now) {
                due = boundary;
            } else if (now - boundary < interval) {
                due = boundary;
            } else {
                due = now;
            }
        }
        final long dueF = due;
        final FrameCallback cbF = callback;
        final Choreographer self = this;
        Runnable r = new Runnable() {
            public void run() {
                self.mLastDueNanos = dueF;
                self.forgetRunnable(cbF, this);
                cbF.doFrame(self.getFrameTimeNanos());
            }
        };
        synchronized (this) {
            java.util.ArrayList<Runnable> list = mPending.get(callback);
            if (list == null) {
                list = new java.util.ArrayList<Runnable>();
                mPending.put(callback, list);
            }
            list.add(r);
        }
        android.os.Message msg = android.os.Message.obtain(mHandler, r);
        mHandler.sendMessageAtTime(msg, dueF / 1000000L);
    }

    private synchronized void forgetRunnable(FrameCallback callback, Runnable r) {
        java.util.ArrayList<Runnable> list = mPending.get(callback);
        if (list != null) {
            list.remove(r);
            if (list.isEmpty()) mPending.remove(callback);
        }
    }

    /** Cancel a pending callback (matched by identity). */
    public void removeFrameCallback(FrameCallback callback) {
        if (callback == null) return;
        java.util.ArrayList<Runnable> list;
        synchronized (this) {
            list = mPending.remove(callback);
        }
        if (list != null && mHandler != null) {
            for (int i = 0; i < list.size(); i++) {
                mHandler.removeCallbacks(list.get(i));
            }
        }
        nativeRemoveFrameCallback(callback);
    }

    /** The display's frame interval in nanoseconds. */
    public long getFrameIntervalNanos() {
        return nativeGetFrameIntervalNanos();
    }

    /** Timestamp of the frame being rendered, or now when none is. */
    public long getFrameTimeNanos() {
        return nativeGetFrameTimeNanos();
    }

    public long getFrameTime() {
        return getFrameTimeNanos() / 1000000L;
    }

    // Native side shares one frame source with the NDK entry points.
    private native void nativePostFrameCallback(FrameCallback callback, long delayMillis);

    private native void nativeRemoveFrameCallback(FrameCallback callback);

    private native long nativeGetFrameIntervalNanos();

    private native long nativeGetFrameTimeNanos();
}
