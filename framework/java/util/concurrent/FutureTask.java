package java.util.concurrent;

public class FutureTask<V> implements RunnableFuture<V> {
    private final Callable<V> callable;
    private V result;
    private Throwable failure;
    private boolean completed;
    private boolean cancelled;
    private Thread runner;

    public FutureTask(Callable<V> callable) {
        if (callable == null) throw new NullPointerException();
        this.callable = callable;
    }

    public FutureTask(final Runnable runnable, final V value) {
        if (runnable == null) throw new NullPointerException();
        callable = new Callable<V>() {
            public V call() { runnable.run(); return value; }
        };
    }

    public void run() { execute(false); }

    protected boolean runAndReset() { return execute(true); }

    private boolean execute(boolean reset) {
        synchronized (this) {
            if (completed || runner != null) return false;
            runner = Thread.currentThread();
        }
        V value = null;
        Throwable error = null;
        try { value = callable.call(); } catch (Throwable t) { error = t; }
        boolean notifyDone = false;
        boolean reusable;
        synchronized (this) {
            runner = null;
            reusable = !completed && error == null && reset;
            if (!completed && !reusable) {
                result = value;
                failure = error;
                completed = true;
                notifyAll();
                notifyDone = true;
            }
        }
        if (notifyDone) done();
        return reusable;
    }

    public boolean cancel(boolean mayInterruptIfRunning) {
        synchronized (this) {
            if (completed) return false;
            cancelled = completed = true;
            if (mayInterruptIfRunning && runner != null) runner.interrupt();
            notifyAll();
        }
        done();
        return true;
    }

    public synchronized boolean isCancelled() { return cancelled; }
    public synchronized boolean isDone() { return completed; }
    protected void done() {}

    protected void set(V value) { finish(value, null); }
    protected void setException(Throwable error) { finish(null, error); }

    private void finish(V value, Throwable error) {
        synchronized (this) {
            if (completed) return;
            result = value;
            failure = error;
            completed = true;
            notifyAll();
        }
        done();
    }

    private V report() throws ExecutionException {
        if (cancelled) throw new CancellationException();
        if (failure != null) throw new ExecutionException(failure);
        return result;
    }

    public synchronized V get() throws InterruptedException, ExecutionException {
        while (!completed) wait();
        return report();
    }

    public synchronized V get(long timeout, TimeUnit unit)
            throws InterruptedException, ExecutionException, TimeoutException {
        long nanos = unit.toNanos(timeout);
        final long start = System.nanoTime();
        while (!completed) {
            long remaining = nanos <= 0 ? 0 : nanos - (System.nanoTime() - start);
            if (remaining <= 0) throw new TimeoutException();
            wait(remaining / 1000000L, (int) (remaining % 1000000L));
        }
        return report();
    }
}
