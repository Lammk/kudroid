package java.util.concurrent;

public final class Executors {

    private Executors() {
    }

    public static ExecutorService newSingleThreadExecutor() {
        return newFixedThreadPool(1);
    }

    public static ExecutorService newFixedThreadPool(int nThreads) {
        return new ThreadPoolExecutor(nThreads, nThreads, 0L, TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<Runnable>());
    }

    public static ExecutorService newCachedThreadPool() {
        return new ThreadPoolExecutor(0, Integer.MAX_VALUE, 60L, TimeUnit.SECONDS,
                new LinkedBlockingQueue<Runnable>());
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
