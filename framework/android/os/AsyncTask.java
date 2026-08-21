package android.os;

import java.util.concurrent.Executor;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * android.os.AsyncTask — chạy doInBackground trên worker thread, đưa kết quả và
 * progress về main thread qua Handler.
 *
 * Đây là hành vi thật: có thread pool, có post về Looper chính, có trạng thái
 * PENDING/RUNNING/FINISHED và chống gọi execute() hai lần.
 */
public abstract class AsyncTask<Params, Progress, Result> {
    /** Trạng thái vòng đời của task. */
    public enum Status {
        PENDING,
        RUNNING,
        FINISHED
    }

    private static final int CORE_POOL_SIZE = 2;
    private static final int MAX_POOL_SIZE = 8;
    private static final int KEEP_ALIVE_SECONDS = 30;

    private static final ThreadFactory sThreadFactory = new ThreadFactory() {
        private final AtomicInteger mCount = new AtomicInteger(1);

        public Thread newThread(Runnable r) {
            Thread t = new Thread(r, "AsyncTask #" + mCount.getAndIncrement());
            t.setDaemon(true);
            return t;
        }
    };

    /** Pool song song (AsyncTask.THREAD_POOL_EXECUTOR trong Android). */
    public static final Executor THREAD_POOL_EXECUTOR = new ThreadPoolExecutor(
            CORE_POOL_SIZE, MAX_POOL_SIZE, KEEP_ALIVE_SECONDS, TimeUnit.SECONDS,
            new LinkedBlockingQueue<Runnable>(), sThreadFactory);

    /** Pool tuần tự — mặc định của execute() từ API 11. */
    public static final Executor SERIAL_EXECUTOR = new ThreadPoolExecutor(
            1, 1, KEEP_ALIVE_SECONDS, TimeUnit.SECONDS,
            new LinkedBlockingQueue<Runnable>(), sThreadFactory);

    private static Handler sMainHandler;

    private static synchronized Handler mainHandler() {
        if (sMainHandler == null) {
            Looper main = Looper.getMainLooper();
            sMainHandler = (main != null) ? new Handler(main) : new Handler();
        }
        return sMainHandler;
    }

    private volatile Status mStatus = Status.PENDING;
    private final AtomicBoolean mCancelled = new AtomicBoolean(false);
    private final AtomicBoolean mTaskInvoked = new AtomicBoolean(false);
    private Result mResult;
    private final Object mDoneLock = new Object();
    private boolean mDone;

    protected abstract Result doInBackground(Params... params);

    protected void onPreExecute() {
    }

    protected void onPostExecute(Result result) {
    }

    protected void onProgressUpdate(Progress... values) {
    }

    protected void onCancelled() {
    }

    protected void onCancelled(Result result) {
        onCancelled();
    }

    public final Status getStatus() {
        return mStatus;
    }

    public final boolean isCancelled() {
        return mCancelled.get();
    }

    public final boolean cancel(boolean mayInterruptIfRunning) {
        return mCancelled.compareAndSet(false, true);
    }

    public final AsyncTask<Params, Progress, Result> execute(Params... params) {
        return executeOnExecutor(SERIAL_EXECUTOR, params);
    }

    public final AsyncTask<Params, Progress, Result> executeOnExecutor(Executor exec,
                                                                      final Params... params) {
        if (mStatus != Status.PENDING) {
            if (mStatus == Status.RUNNING) {
                throw new IllegalStateException(
                        "Cannot execute task: the task is already running.");
            }
            throw new IllegalStateException(
                    "Cannot execute task: the task has already been executed (a task can be executed only once)");
        }
        mStatus = Status.RUNNING;
        onPreExecute();

        exec.execute(new Runnable() {
            public void run() {
                mTaskInvoked.set(true);
                Result result = null;
                try {
                    if (!isCancelled()) {
                        result = doInBackground(params);
                    }
                } catch (Throwable t) {
                    // Exception trong doInBackground phải huỷ task chứ không
                    // được làm chết worker thread của pool.
                    mCancelled.set(true);
                    android.util.Log.e("AsyncTask", "doInBackground threw: " + t);
                } finally {
                    postResult(result);
                }
            }
        });
        return this;
    }

    protected final void publishProgress(final Progress... values) {
        if (isCancelled()) return;
        mainHandler().post(new Runnable() {
            public void run() {
                onProgressUpdate(values);
            }
        });
    }

    private void postResult(final Result result) {
        mainHandler().post(new Runnable() {
            public void run() {
                mResult = result;
                if (isCancelled()) {
                    onCancelled(result);
                } else {
                    onPostExecute(result);
                }
                mStatus = Status.FINISHED;
                synchronized (mDoneLock) {
                    mDone = true;
                    mDoneLock.notifyAll();
                }
            }
        });
    }

    /** Chặn tới khi task xong. Không được gọi từ main thread (sẽ deadlock). */
    public final Result get() throws InterruptedException {
        synchronized (mDoneLock) {
            while (!mDone) {
                mDoneLock.wait();
            }
        }
        return mResult;
    }

    public final Result get(long timeout, TimeUnit unit) throws InterruptedException {
        final long deadline = System.nanoTime() + unit.toNanos(timeout);
        synchronized (mDoneLock) {
            while (!mDone) {
                long remainingMs = (deadline - System.nanoTime()) / 1000000L;
                if (remainingMs <= 0) break;
                mDoneLock.wait(remainingMs);
            }
        }
        return mResult;
    }

    public static void execute(Runnable runnable) {
        THREAD_POOL_EXECUTOR.execute(runnable);
    }
}
