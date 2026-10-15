package java.util.concurrent;

/**
 * One-thread-per-task Executor: simplified compared to real pool but correct language
 * means the code just needs to "run in the background and forget".
 */
public class ThreadPoolExecutor implements ExecutorService {

    private final ThreadFactory factory;
    private final java.util.ArrayList<Thread> running = new java.util.ArrayList<Thread>();
    private volatile boolean shutdown;

    public ThreadPoolExecutor(int corePoolSize, int maximumPoolSize, long keepAliveTime,
            TimeUnit unit, BlockingQueue<Runnable> workQueue) {
        this(corePoolSize, maximumPoolSize, keepAliveTime, unit, workQueue, null);
    }

    public ThreadPoolExecutor(int corePoolSize, int maximumPoolSize, long keepAliveTime,
            TimeUnit unit, BlockingQueue<Runnable> workQueue, ThreadFactory threadFactory) {
        this.factory = threadFactory;
    }

    public void execute(Runnable command) {
        if (command == null) {
            throw new NullPointerException();
        }
        if (shutdown) {
            throw new RejectedExecutionException("executor has shut down");
        }
        Thread t = factory != null ? factory.newThread(command) : new Thread(command);
        synchronized (running) {
            running.add(t);
        }
        t.start();
    }

    public <T> Future<T> submit(Callable<T> task) {
        FutureTask<T> future = new FutureTask<T>(task);
        execute(future);
        return future;
    }

    public Future<?> submit(Runnable task) {
        FutureTask<Object> future = new FutureTask<Object>(new RunnableCallable(task));
        execute(future);
        return future;
    }

    public void shutdown() {
        shutdown = true;
    }

    public java.util.List<Runnable> shutdownNow() {
        shutdown = true;
        return new java.util.ArrayList<Runnable>();
    }

    public boolean isShutdown() {
        return shutdown;
    }

    public boolean isTerminated() {
        synchronized (running) {
            for (int i = 0; i < running.size(); i++) {
                if (running.get(i).isAlive()) {
                    return false;
                }
            }
        }
        return shutdown;
    }

    public boolean awaitTermination(long timeout, TimeUnit unit) throws InterruptedException {
        long millis = unit.toMillis(timeout);
        java.util.ArrayList<Thread> snapshot;
        synchronized (running) {
            snapshot = new java.util.ArrayList<Thread>(running);
        }
        for (int i = 0; i < snapshot.size(); i++) {
            snapshot.get(i).join(millis);
        }
        return isTerminated();
    }

    public int getActiveCount() {
        int n = 0;
        synchronized (running) {
            for (int i = 0; i < running.size(); i++) {
                if (running.get(i).isAlive()) {
                    n++;
                }
            }
        }
        return n;
    }

    public void setThreadFactory(ThreadFactory f) {
    }

    public void allowCoreThreadTimeOut(boolean value) {
    }

    private static final class RunnableCallable implements Callable<Object> {

        private final Runnable task;

        RunnableCallable(Runnable task) {
            this.task = task;
        }

        public Object call() {
            task.run();
            return null;
        }
    }
}
