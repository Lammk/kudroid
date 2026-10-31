package java.util.concurrent;

public final class Executors {

    private Executors() {
    }

    public static ExecutorService newSingleThreadExecutor() {
        return unconfigurableExecutorService(newFixedThreadPool(1));
    }

    public static ExecutorService newSingleThreadExecutor(ThreadFactory factory) {
        return unconfigurableExecutorService(newFixedThreadPool(1, factory));
    }

    public static ExecutorService newFixedThreadPool(int nThreads) {
        if (nThreads <= 0) throw new IllegalArgumentException("nThreads <= 0");
        return new ThreadPoolExecutor(nThreads, nThreads, 0L, TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<Runnable>());
    }

    public static ExecutorService newFixedThreadPool(int nThreads, ThreadFactory factory) {
        if (nThreads <= 0) throw new IllegalArgumentException("nThreads <= 0");
        if (factory == null) throw new NullPointerException("threadFactory == null");
        return new ThreadPoolExecutor(nThreads, nThreads, 0L, TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<Runnable>(), factory);
    }

    public static ScheduledExecutorService newScheduledThreadPool(int corePoolSize) {
        return new ScheduledThreadPoolExecutor(corePoolSize);
    }

    public static ScheduledExecutorService newScheduledThreadPool(int corePoolSize,
                                                                  ThreadFactory factory) {
        if (factory == null) throw new NullPointerException("threadFactory == null");
        return new ScheduledThreadPoolExecutor(corePoolSize, factory);
    }

    public static ScheduledExecutorService newSingleThreadScheduledExecutor() {
        return unconfigurableScheduledExecutorService(new ScheduledThreadPoolExecutor(1));
    }

    public static ScheduledExecutorService newSingleThreadScheduledExecutor(ThreadFactory factory) {
        if (factory == null) throw new NullPointerException("threadFactory == null");
        return unconfigurableScheduledExecutorService(new ScheduledThreadPoolExecutor(1, factory));
    }

    public static ExecutorService unconfigurableExecutorService(ExecutorService executor) {
        if (executor == null) throw new NullPointerException();
        return new DelegatedExecutorService(executor);
    }

    public static ScheduledExecutorService unconfigurableScheduledExecutorService(
            ScheduledExecutorService executor) {
        if (executor == null) throw new NullPointerException();
        return new DelegatedScheduledExecutorService(executor);
    }

    private static class DelegatedExecutorService implements ExecutorService {
        private final ExecutorService executor;

        DelegatedExecutorService(ExecutorService executor) { this.executor = executor; }
        public void execute(Runnable command) { executor.execute(command); }
        public void shutdown() { executor.shutdown(); }
        public java.util.List<Runnable> shutdownNow() { return executor.shutdownNow(); }
        public boolean isShutdown() { return executor.isShutdown(); }
        public boolean isTerminated() { return executor.isTerminated(); }
        public boolean awaitTermination(long timeout, TimeUnit unit) throws InterruptedException {
            return executor.awaitTermination(timeout, unit);
        }
        public <T> Future<T> submit(Callable<T> task) { return executor.submit(task); }
        public Future<?> submit(Runnable task) { return executor.submit(task); }
        public <T> Future<T> submit(Runnable task, T result) { return executor.submit(task, result); }
    }

    private static final class DelegatedScheduledExecutorService extends DelegatedExecutorService
            implements ScheduledExecutorService {
        private final ScheduledExecutorService executor;

        DelegatedScheduledExecutorService(ScheduledExecutorService executor) {
            super(executor);
            this.executor = executor;
        }
        public ScheduledFuture<?> schedule(Runnable command, long delay, TimeUnit unit) {
            return executor.schedule(command, delay, unit);
        }
        public <V> ScheduledFuture<V> schedule(Callable<V> task, long delay, TimeUnit unit) {
            return executor.schedule(task, delay, unit);
        }
        public ScheduledFuture<?> scheduleAtFixedRate(Runnable command, long initialDelay,
                                                      long period, TimeUnit unit) {
            return executor.scheduleAtFixedRate(command, initialDelay, period, unit);
        }
        public ScheduledFuture<?> scheduleWithFixedDelay(Runnable command, long initialDelay,
                                                         long delay, TimeUnit unit) {
            return executor.scheduleWithFixedDelay(command, initialDelay, delay, unit);
        }
    }

    public static ExecutorService newCachedThreadPool() {
        return new ThreadPoolExecutor(0, Integer.MAX_VALUE, 60L, TimeUnit.SECONDS,
                new SynchronousQueue<Runnable>());
    }

    public static ExecutorService newCachedThreadPool(ThreadFactory factory) {
        if (factory == null) throw new NullPointerException("threadFactory == null");
        return new ThreadPoolExecutor(0, Integer.MAX_VALUE, 60L, TimeUnit.SECONDS,
                new SynchronousQueue<Runnable>(), factory);
    }

    public static ThreadFactory defaultThreadFactory() {
        return new DefaultFactory();
    }

    private static final class DefaultFactory implements ThreadFactory {

        public Thread newThread(Runnable r) {
            return new Thread(r);
        }
    }
}
