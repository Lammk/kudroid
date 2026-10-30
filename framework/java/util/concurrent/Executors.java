package java.util.concurrent;

public final class Executors {

    private Executors() {
    }

    public static ExecutorService newSingleThreadExecutor() {
        return newFixedThreadPool(1);
    }

    public static ExecutorService newSingleThreadExecutor(ThreadFactory factory) {
        return newFixedThreadPool(1, factory);
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
        return new ScheduledThreadPoolExecutor(1);
    }

    public static ScheduledExecutorService newSingleThreadScheduledExecutor(ThreadFactory factory) {
        if (factory == null) throw new NullPointerException("threadFactory == null");
        return new ScheduledThreadPoolExecutor(1, factory);
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
