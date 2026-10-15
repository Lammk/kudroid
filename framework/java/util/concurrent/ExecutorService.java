package java.util.concurrent;

public interface ExecutorService extends Executor {

    void shutdown();

    java.util.List<Runnable> shutdownNow();

    boolean isShutdown();

    boolean isTerminated();

    boolean awaitTermination(long timeout, TimeUnit unit) throws InterruptedException;

    <T> Future<T> submit(Callable<T> task);

    Future<?> submit(Runnable task);
}
