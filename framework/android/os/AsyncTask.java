package android.os;

import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

public abstract class AsyncTask<Params, Progress, Result> {
    public static final Executor THREAD_POOL_EXECUTOR = Executors.newCachedThreadPool();
    public static final Executor SERIAL_EXECUTOR = Executors.newSingleThreadExecutor();

    public enum Status { PENDING, RUNNING, FINISHED }
    private volatile Status mStatus = Status.PENDING;

    public AsyncTask() {}
    public final Status getStatus() { return mStatus; }
    protected void onPreExecute() {}
    @SuppressWarnings("unchecked")
    protected abstract Result doInBackground(Params... params);
    protected void onPostExecute(Result result) {}
    @SuppressWarnings("unchecked")
    protected void onProgressUpdate(Progress... values) {}
    protected void onCancelled(Result result) { onCancelled(); }
    protected void onCancelled() {}
    private volatile boolean mCancelled = false;
    public final boolean isCancelled() { return mCancelled; }
    public final boolean cancel(boolean mayInterruptIfRunning) {
        mCancelled = true;
        return true;
    }

    @SafeVarargs
    public final AsyncTask<Params, Progress, Result> execute(Params... params) {
        return executeOnExecutor(THREAD_POOL_EXECUTOR, params);
    }

    @SafeVarargs
    public final AsyncTask<Params, Progress, Result> executeOnExecutor(Executor exec, final Params... params) {
        mStatus = Status.RUNNING;
        onPreExecute();
        final Handler ui = new Handler(Looper.getMainLooper());
        exec.execute(new Runnable() {
            public void run() {
                final Result r = doInBackground(params);
                mStatus = Status.FINISHED;
                // Callbacks touch Views: they must run on the UI thread.
                ui.post(new Runnable() {
                    public void run() {
                        if (mCancelled) {
                            onCancelled(r);
                        } else {
                            onPostExecute(r);
                        }
                    }
                });
            }
        });
        return this;
    }

    @SafeVarargs
    protected final void publishProgress(final Progress... values) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            public void run() {
                onProgressUpdate(values);
            }
        });
    }
}
