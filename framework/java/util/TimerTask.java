package java.util;

public abstract class TimerTask implements Runnable {
    final Object lock = new Object();
    int state = 0;
    static final int VIRGIN = 0;
    static final int SCHEDULED = 1;
    static final int EXECUTED = 2;
    static final int CANCELLED = 3;
    long nextExecutionTime;
    long period = 0;

    protected TimerTask() {}
    public abstract void run();
    public boolean cancel() {
        synchronized(lock) {
            boolean result = (state == SCHEDULED);
            state = CANCELLED;
            return result;
        }
    }
    public long scheduledExecutionTime() {
        synchronized(lock) {
            return (period < 0) ? (nextExecutionTime + period)
                                : (nextExecutionTime - period);
        }
    }
}
