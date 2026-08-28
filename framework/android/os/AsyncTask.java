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
    public final boolean isCancelled() { return false; }
    public final boolean cancel(boolean mayInterruptIfRunning) { return false; }

    @SafeVarargs
    public final AsyncTask<Params, Progress, Result> execute(Params... params) {
        return executeOnExecutor(THREAD_POOL_EXECUTOR, params);
    }

    @SafeVarargs
    public final AsyncTask<Params, Progress, Result> executeOnExecutor(Executor exec, final Params... params) {
        mStatus = Status.RUNNING;
        onPreExecute();
        exec.execute(new Runnable() {
            public void run() {
                final Result r = doInBackground(params);
                mStatus = Status.FINISHED;
                onPostExecute(r);
            }
        });
        return this;
    }

    @SafeVarargs
    protected final void publishProgress(Progress... values) {
        onProgressUpdate(values);
    }
}
