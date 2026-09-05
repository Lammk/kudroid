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
     */
    public void postFrameCallbackDelayed(FrameCallback callback, long delayMillis) {
        if (callback == null) {
            throw new IllegalArgumentException("callback must not be null");
        }
        nativePostFrameCallback(callback, delayMillis < 0L ? 0L : delayMillis);
    }

    /** Cancel a pending callback (matched by identity). */
    public void removeFrameCallback(FrameCallback callback) {
        if (callback == null) return;
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
