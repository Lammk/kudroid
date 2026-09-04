package android.view;

/**
 * Frame callbacks, the Java half.
 *
 * Why this class has a body now. KuDroid implemented the NDK side
 * (AChoreographer_getInstance and friends, src/platform/FramePacer.cpp) and ULTRAKILL
 * still stopped — because Unity uses BOTH APIs, and this one was a nine-line stub:
 *
 *     android.view.Choreographer->getInstance()Landroid/view/Choreographer;
 *     android.view.Choreographer->postFrameCallback(...FrameCallback;)V
 *     android.view.Choreographer->postFrameCallbackDelayed(...FrameCallback;J)V
 *
 * All three were absent, so Interpreter::ResolveMethod auto-stubbed them: the class
 * exists, only the methods are missing, which produces a bodyless DexMethod rather
 * than a NoSuchMethodError. getInstance() therefore returned null, and Unity's
 * JNIBridge answered by throwing NoSuchMethodError itself — the string
 * "JNIBridge error: Java interface default methods are only supported since Android
 * Oreo" is in ULTRAKILL's own classes.dex, next to Ljava/lang/NoSuchMethodError;.
 *
 * That exception went uncaught out of Looper.loop, ActivityThread.main returned, and
 * the shell printed "Session ended" while FMOD's audio thread kept running.
 *
 * The frames come from the same pacer the NDK entry points use, deliberately. Two
 * independent frame sources in one process means two clocks: a guest that posts on
 * both would see timestamps that disagree, and pace itself against whichever it read
 * last. FrameCallback.doFrame receives the identical nanosecond value
 * AChoreographer's callback would, on CLOCK_MONOTONIC — the clock the guest reads
 * back through System.nanoTime.
 */
public final class Choreographer {

    /**
     * A frame callback.
     *
     * The interface previously declared no methods at all, which is worse than the
     * class being missing: postFrameCallback could be handed an implementation and
     * there would be nothing on it to call. doFrame's parameter is nanoseconds on the
     * same timeline as System.nanoTime().
     */
    public interface FrameCallback {
        void doFrame(long frameTimeNanos);
    }

    /**
     * One instance per thread, as on Android.
     *
     * A ThreadLocal rather than a process-wide singleton because Android's is
     * per-thread and apps depend on that: a guest posts from its render thread and
     * from the main thread, and the two must not share a queue. The native side keys
     * its instances the same way.
     */
    private static final ThreadLocal<Choreographer> sThreadInstance =
            new ThreadLocal<Choreographer>() {
                @Override
                protected Choreographer initialValue() {
                    return new Choreographer();
                }
            };

    /**
     * Handle to the native Choreographer that owns this thread's frame queue — the
     * very object AChoreographer_getInstance returns. Zero until the first post.
     */
    private long nativeInstance;

    private Choreographer() {}

    /**
     * This thread's Choreographer. Never null.
     *
     * Android returns null only for a thread with no Looper, and callers treat that as
     * fatal — which is exactly what happened here when the auto-stub returned null:
     * Unity read it as "this thread cannot receive frames" and gave up. KuDroid's
     * looper is created on demand, so the precondition always holds.
     */
    public static Choreographer getInstance() {
        return sThreadInstance.get();
    }

    /**
     * Run {@code callback} once, on the next frame.
     *
     * One-shot, as on Android: a caller that wants continuous frames posts again from
     * inside its own callback. Re-arming automatically would make a guest render twice
     * per frame.
     */
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

    /**
     * Cancel a pending callback.
     *
     * Matched by identity, which is what Android does: the same object posted twice is
     * queued twice, and removing it clears both entries.
     */
    public void removeFrameCallback(FrameCallback callback) {
        if (callback == null) return;
        nativeRemoveFrameCallback(callback);
    }

    /**
     * The display's frame interval in nanoseconds.
     *
     * Hidden on the platform (@UnsupportedAppUsage) but read by frame pacers through
     * reflection, and cheap to answer honestly since the pacer already knows the real
     * refresh rate. Reporting a constant 16.67ms on a 120 Hz panel would make a pacer
     * aim at a deadline twice as far away as the real one, every frame.
     */
    public long getFrameIntervalNanos() {
        return nativeGetFrameIntervalNanos();
    }

    /**
     * The timestamp of the frame currently being rendered, or now when none is.
     *
     * Android throws IllegalStateException when called outside a frame callback. That
     * is deliberately not copied: apps call it defensively from arbitrary threads, and
     * an exception where the platform's own docs say "the value is only valid inside a
     * callback" turns a diagnostic read into a crash.
     */
    public long getFrameTimeNanos() {
        return nativeGetFrameTimeNanos();
    }

    public long getFrameTime() {
        return getFrameTimeNanos() / 1000000L;
    }

    // Native side: src/kuart/LibCore.cpp, forwarding to the same FramePacer the NDK
    // AChoreographer_* entry points use, so both APIs share one frame source.
    private native void nativePostFrameCallback(FrameCallback callback, long delayMillis);

    private native void nativeRemoveFrameCallback(FrameCallback callback);

    private native long nativeGetFrameIntervalNanos();

    private native long nativeGetFrameTimeNanos();
}
