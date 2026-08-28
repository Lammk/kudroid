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
