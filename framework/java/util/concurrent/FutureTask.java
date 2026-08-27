package java.util.concurrent;

public class FutureTask<V> implements Future<V>, Runnable {

    private final Callable<V> callable;
    private V result;
    private Throwable failure;
    private boolean done;
    private boolean cancelled;

    public FutureTask(Callable<V> callable) {
        this.callable = callable;
    }

    public void run() {
        V value = null;
        Throwable error = null;
        try {
            if (!cancelled) {
                value = callable.call();
            }
        } catch (Throwable t) {
            error = t;
        }
        synchronized (this) {
            result = value;
            failure = error;
            done = true;
            notifyAll();
        }
    }

    public synchronized boolean cancel(boolean mayInterruptIfRunning) {
        if (done) {
            return false;
        }
        cancelled = true;
        done = true;
        notifyAll();
        return true;
    }

    public synchronized boolean isCancelled() {
        return cancelled;
    }

    public synchronized boolean isDone() {
        return done;
    }

    public synchronized V get() throws InterruptedException, ExecutionException {
        while (!done) {
            wait();
        }
        if (failure != null) {
            throw new ExecutionException(failure);
        }
        return result;
    }

    public synchronized V get(long timeout, TimeUnit unit)
            throws InterruptedException, ExecutionException {
        long deadline = System.currentTimeMillis() + unit.toMillis(timeout);
        while (!done) {
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0) {
                return null;
            }
            wait(remaining);
        }
        if (failure != null) {
            throw new ExecutionException(failure);
        }
        return result;
    }
}
