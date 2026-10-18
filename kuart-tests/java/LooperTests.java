import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;

/** A message posted from another thread reaches a HandlerThread's callback on that thread. */
public final class LooperTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    private static final Object lock = new Object();
    private static int delivered;
    private static int lastWhat;
    private static int lastArg1;
    private static int lastArg2;
    private static Thread lastThread;

    private static boolean awaitDelivered(int count, long timeoutMs) throws Exception {
        long end = System.currentTimeMillis() + timeoutMs;
        synchronized (lock) {
            while (delivered < count) {
                long left = end - System.currentTimeMillis();
                if (left <= 0) return false;
                lock.wait(left);
            }
        }
        return true;
    }

    public static String run() {
        TestSupport.report.setLength(0);
        try { handlerThread(); } catch (Throwable t) { TestSupport.fail("handlerThread", t); }
        return TestSupport.report.toString();
    }

    static void handlerThread() throws Exception {
        HandlerThread thread = new HandlerThread("VideoDecoderLooper");
        thread.start();
        check(thread.isAlive(), "a started HandlerThread is alive");
        Looper looper = thread.getLooper();
        check(looper != null, "getLooper returns the thread's looper");
        check(looper != Looper.myLooper(), "it is not the caller's looper");

        Handler handler = new Handler(looper, new Handler.Callback() {
            public boolean handleMessage(Message msg) {
                synchronized (lock) {
                    delivered++;
                    lastWhat = msg.what;
                    lastArg1 = msg.arg1;
                    lastArg2 = msg.arg2;
                    lastThread = Thread.currentThread();
                    lock.notifyAll();
                }
                return true;
            }
        });

        // The call shape the video player uses: obtainMessage(what, arg1, arg2).sendToTarget().
        handler.obtainMessage(3, 42, 7).sendToTarget();
        check(awaitDelivered(1, 5000), "obtainMessage(...).sendToTarget() reaches the callback");
        check(lastWhat == 3 && lastArg1 == 42 && lastArg2 == 7, "with its what/arg1/arg2");
        check(lastThread == (Thread) thread, "on the HandlerThread");

        // From a different thread than the one that created the handler.
        final Handler h = handler;
        Thread poster = new Thread(new Runnable() {
            public void run() { h.obtainMessage(4, 1, 2).sendToTarget(); }
        });
        poster.start();
        check(awaitDelivered(2, 5000), "a message posted from another thread is delivered");
        check(lastWhat == 4, "and is the one that was posted");

        // Several in a row keep their order.
        for (int i = 0; i < 20; i++) handler.obtainMessage(100 + i, i, 0).sendToTarget();
        check(awaitDelivered(22, 5000), "a burst of messages is delivered");
        check(lastWhat == 119, "in order");

        check(thread.quit(), "the thread can be asked to quit");
    }
}
