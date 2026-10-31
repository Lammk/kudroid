package java.util.concurrent;

import java.util.ArrayList;
import java.util.List;

public class ScheduledThreadPoolExecutor extends ThreadPoolExecutor implements ScheduledExecutorService {
    private final DelayQueue<RunnableScheduledFuture<?>> tasks;
    private final ArrayList<Worker> workers = new ArrayList<Worker>();
    private final int coreSize;
    private long sequence;
    private boolean stopped;
    private boolean immediateStop;
    private boolean continuePeriodic;
    private boolean continueDelayed = true;
    private volatile boolean removeCancelled;
    private int active;

    public ScheduledThreadPoolExecutor(int corePoolSize) {
        this(corePoolSize, Executors.defaultThreadFactory());
    }
    public ScheduledThreadPoolExecutor(int corePoolSize, ThreadFactory factory) {
        this(corePoolSize, factory, new DelayQueue<RunnableScheduledFuture<?>>());
    }
    @SuppressWarnings({"unchecked", "rawtypes"})
    private ScheduledThreadPoolExecutor(int size, ThreadFactory factory,
                                        DelayQueue<RunnableScheduledFuture<?>> queue) {
        super(size, Integer.MAX_VALUE, 0, TimeUnit.NANOSECONDS, (BlockingQueue) queue, factory);
        tasks = queue;
        coreSize = size;
    }

    private long triggerTime(long delay) {
        delay = Math.max(0, delay);
        if (delay >= Long.MAX_VALUE / 2) {
            RunnableScheduledFuture<?> first = tasks.peek();
            if (first != null) {
                long headDelay = first.getDelay(TimeUnit.NANOSECONDS);
                if (headDelay < 0 && delay - headDelay < 0) delay = Long.MAX_VALUE + headDelay;
            }
        }
        return System.nanoTime() + delay;
    }
    private synchronized long nextSequence() { return sequence++; }
    private boolean permitted(RunnableScheduledFuture<?> task) {
        return !immediateStop && (!stopped || (task.isPeriodic() ? continuePeriodic : continueDelayed));
    }
    private synchronized void enqueue(RunnableScheduledFuture<?> task, boolean repeat) {
        if ((!repeat && stopped) || (repeat && !permitted(task))) {
            task.cancel(false);
            if (!repeat) throw new RejectedExecutionException("executor has shut down");
            return;
        }
        if (task.isCancelled()) return;
        tasks.offer(task);
        if (workers.size() < Math.max(1, coreSize)) {
            Worker worker = new Worker();
            Thread thread = getThreadFactory().newThread(worker);
            if (thread == null) return;
            worker.thread = thread;
            workers.add(worker);
            try { thread.start(); } catch (RuntimeException e) {
                workers.remove(worker); tasks.remove(task); task.cancel(false); throw e;
            } catch (Error e) {
                workers.remove(worker); tasks.remove(task); task.cancel(false); throw e;
            }
        }
    }
    protected <V> RunnableScheduledFuture<V> decorateTask(Runnable r, RunnableScheduledFuture<V> task) {
        return task;
    }
    protected <V> RunnableScheduledFuture<V> decorateTask(Callable<V> c, RunnableScheduledFuture<V> task) {
        return task;
    }
    public ScheduledFuture<?> schedule(Runnable command, long delay, TimeUnit unit) {
        if (command == null || unit == null) throw new NullPointerException();
        ScheduledTask<Void> task = new ScheduledTask<Void>(command, null, delay, unit, 0);
        task.outer = decorateTask(command, task);
        enqueue(task.outer, false);
        return task.outer;
    }
    public <V> ScheduledFuture<V> schedule(Callable<V> callable, long delay, TimeUnit unit) {
        if (callable == null || unit == null) throw new NullPointerException();
        ScheduledTask<V> task = new ScheduledTask<V>(callable, delay, unit);
        task.outer = decorateTask(callable, task);
        enqueue(task.outer, false);
        return task.outer;
    }
    public ScheduledFuture<?> scheduleAtFixedRate(Runnable command, long initialDelay, long period, TimeUnit unit) {
        if (command == null || unit == null) throw new NullPointerException();
        if (period <= 0) throw new IllegalArgumentException();
        ScheduledTask<Void> task = new ScheduledTask<Void>(command, null, initialDelay, unit, unit.toNanos(period));
        task.outer = decorateTask(command, task);
        enqueue(task.outer, false);
        return task.outer;
    }
    public ScheduledFuture<?> scheduleWithFixedDelay(Runnable command, long initialDelay, long delay, TimeUnit unit) {
        if (command == null || unit == null) throw new NullPointerException();
        if (delay <= 0) throw new IllegalArgumentException();
        ScheduledTask<Void> task = new ScheduledTask<Void>(command, null, initialDelay, unit, -unit.toNanos(delay));
        task.outer = decorateTask(command, task);
        enqueue(task.outer, false);
        return task.outer;
    }
    public void execute(Runnable command) { schedule(command, 0, TimeUnit.NANOSECONDS); }
    public Future<?> submit(Runnable task) { return schedule(task, 0, TimeUnit.NANOSECONDS); }
    public <V> Future<V> submit(Callable<V> task) { return schedule(task, 0, TimeUnit.NANOSECONDS); }
    public <V> Future<V> submit(Runnable command, V result) {
        if (command == null) throw new NullPointerException();
        ScheduledTask<V> task = new ScheduledTask<V>(command, result, 0, TimeUnit.NANOSECONDS, 0);
        task.outer = decorateTask(command, task);
        enqueue(task.outer, false);
        return task.outer;
    }

    private final class ScheduledTask<V> extends FutureTask<V> implements RunnableScheduledFuture<V> {
        private volatile long time;
        private final long period;
        private final long order = nextSequence();
        private RunnableScheduledFuture<V> outer = this;
        ScheduledTask(Runnable command, V value, long delay, TimeUnit unit, long period) {
            super(command, value);
            this.time = triggerTime(unit.toNanos(delay));
            this.period = period;
        }
        ScheduledTask(Callable<V> callable, long delay, TimeUnit unit) {
            super(callable);
            time = triggerTime(unit.toNanos(delay));
            period = 0;
        }
        public boolean isPeriodic() { return period != 0; }
        public long getDelay(TimeUnit unit) { return unit.convert(time - System.nanoTime(), TimeUnit.NANOSECONDS); }
        public int compareTo(Delayed other) {
            if (other == this) return 0;
            if (other instanceof ScheduledThreadPoolExecutor.ScheduledTask) {
                ScheduledTask<?> task = (ScheduledTask<?>) other;
                long difference = time - task.time;
                if (difference != 0) return difference < 0 ? -1 : 1;
                return order < task.order ? -1 : order == task.order ? 0 : 1;
            }
            long difference = getDelay(TimeUnit.NANOSECONDS) - other.getDelay(TimeUnit.NANOSECONDS);
            return difference < 0 ? -1 : difference > 0 ? 1 : 0;
        }
        public boolean cancel(boolean interrupt) {
            boolean changed = super.cancel(interrupt);
            if (changed && removeCancelled) {
                tasks.remove(outer);
                wakeIdle();
            }
            return changed;
        }
        public void run() {
            synchronized (ScheduledThreadPoolExecutor.this) {
                if (!permitted(outer)) { cancel(false); return; }
            }
            if (period == 0) super.run();
            else if (super.runAndReset()) {
                time = period > 0 ? time + period : triggerTime(-period);
                enqueue(outer, true);
            }
        }
    }
    private final class Worker implements Runnable {
        Thread thread;
        boolean busy;
        public void run() {
            try {
                while (true) {
                    synchronized (ScheduledThreadPoolExecutor.this) {
                        if (immediateStop || (stopped && tasks.isEmpty())) return;
                    }
                    RunnableScheduledFuture<?> task;
                    try { task = tasks.take(); } catch (InterruptedException e) { continue; }
                    synchronized (ScheduledThreadPoolExecutor.this) {
                        if (!permitted(task)) { task.cancel(false); continue; }
                        busy = true;
                        active++;
                        if (!immediateStop) Thread.interrupted();
                    }
                    try { task.run(); } finally {
                        synchronized (ScheduledThreadPoolExecutor.this) {
                            busy = false;
                            active--;
                        }
                    }
                }
            } finally {
                synchronized (ScheduledThreadPoolExecutor.this) {
                    workers.remove(this);
                    ScheduledThreadPoolExecutor.this.notifyAll();
                }
            }
        }
    }
    private synchronized void wakeIdle() {
        for (Worker worker : workers) if (!worker.busy) worker.thread.interrupt();
    }
    private void cleanQueue() {
        for (RunnableScheduledFuture<?> task : tasks) {
            if (task.isCancelled() || !permitted(task)) { task.cancel(false); tasks.remove(task); }
        }
        wakeIdle();
        notifyAll();
    }
    public synchronized void shutdown() { stopped = true; cleanQueue(); }
    public synchronized List<Runnable> shutdownNow() {
        stopped = immediateStop = true;
        List<Runnable> pending = new ArrayList<Runnable>();
        for (RunnableScheduledFuture<?> task : tasks) { pending.add(task); task.cancel(false); }
        tasks.clear();
        for (Worker worker : workers) worker.thread.interrupt();
        notifyAll();
        return pending;
    }
    public synchronized boolean isShutdown() { return stopped; }
    public synchronized boolean isTerminated() { return stopped && workers.isEmpty(); }
    public synchronized int getActiveCount() { return active; }
    public synchronized int getPoolSize() { return workers.size(); }
    public int getCorePoolSize() { return coreSize; }
    public synchronized boolean awaitTermination(long timeout, TimeUnit unit) throws InterruptedException {
        long nanos = unit.toNanos(timeout), start = System.nanoTime();
        while (!isTerminated()) {
            long remaining = nanos <= 0 ? 0 : nanos - (System.nanoTime() - start);
            if (remaining <= 0) return false;
            wait(remaining / 1000000L, (int) (remaining % 1000000L));
        }
        return true;
    }
    public void setRemoveOnCancelPolicy(boolean value) { removeCancelled = value; }
    public boolean getRemoveOnCancelPolicy() { return removeCancelled; }
    public synchronized void setContinueExistingPeriodicTasksAfterShutdownPolicy(boolean value) {
        continuePeriodic = value;
        if (stopped) cleanQueue();
    }
    public synchronized boolean getContinueExistingPeriodicTasksAfterShutdownPolicy() { return continuePeriodic; }
    public synchronized void setExecuteExistingDelayedTasksAfterShutdownPolicy(boolean value) {
        continueDelayed = value;
        if (stopped) cleanQueue();
    }
    public synchronized boolean getExecuteExistingDelayedTasksAfterShutdownPolicy() { return continueDelayed; }
}
