package java.util.concurrent;

import java.io.Serializable;

public class Semaphore implements Serializable {
    private static final long serialVersionUID = -3222578661600680210L;
    private int permits;

    public Semaphore(int permits) {
        this.permits = permits;
    }

    public Semaphore(int permits, boolean fair) {
        this.permits = permits;
    }

    public synchronized void acquire() throws InterruptedException {
        while (permits <= 0) {
            wait();
        }
        permits--;
    }

    public synchronized void acquire(int permits) throws InterruptedException {
        if (permits < 0) throw new IllegalArgumentException();
        while (this.permits < permits) {
            wait();
        }
        this.permits -= permits;
    }

    public synchronized boolean tryAcquire() {
        if (permits > 0) {
            permits--;
            return true;
        }
        return false;
    }

    /**
     * Timed acquire. Was absent, so the runtime auto-stubbed it (false
     * forever) — and Unity's pause handshake, which waits on exactly this
     * with 2000ms, could never complete: "Timeout while trying to pause",
     * forced QUIT, then teardown crashed inside nativeDone with workers
     * still running. Same wait/notify mechanics as acquire().
     */
    public boolean tryAcquire(long timeout, java.util.concurrent.TimeUnit unit)
            throws InterruptedException {
        if (unit == null) throw new NullPointerException();
        if (timeout <= 0) return tryAcquire();
        long deadline = System.currentTimeMillis() + unit.toMillis(timeout);
        synchronized (this) {
            for (;;) {
                if (permits > 0) {
                    permits--;
                    return true;
                }
                long left = deadline - System.currentTimeMillis();
                if (left <= 0) return false;
                wait(left);
            }
        }
    }

    public synchronized void release() {
        permits++;
        notifyAll();
    }

    public synchronized void release(int permits) {
        if (permits < 0) throw new IllegalArgumentException();
        this.permits += permits;
        notifyAll();
    }

    public synchronized int availablePermits() {
        return permits;
    }
}
