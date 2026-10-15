package java.util.concurrent.locks;

import java.util.concurrent.TimeUnit;
import java.util.Date;
import java.io.Serializable;

public class ReentrantLock implements Lock, Serializable {
    private static final long serialVersionUID = 7373984872572414699L;
    private Thread owner = null;
    private int holdCount = 0;

    public ReentrantLock() {}
    public ReentrantLock(boolean fair) {}

    public synchronized void lock() {
        Thread current = Thread.currentThread();
        while (owner != null && owner != current) {
            try {
                wait();
            } catch (InterruptedException e) {
                // reentrant lock is uninterruptible by default
            }
        }
        owner = current;
        holdCount++;
    }

    public synchronized void lockInterruptibly() throws InterruptedException {
        Thread current = Thread.currentThread();
        while (owner != null && owner != current) {
            wait();
        }
        owner = current;
        holdCount++;
    }

    public synchronized boolean tryLock() {
        Thread current = Thread.currentThread();
        if (owner == null || owner == current) {
            owner = current;
            holdCount++;
            return true;
        }
        return false;
    }

    public synchronized boolean tryLock(long timeout, TimeUnit unit) throws InterruptedException {
        return tryLock();
    }

    public synchronized void unlock() {
        if (Thread.currentThread() != owner) {
            throw new IllegalMonitorStateException();
        }
        holdCount--;
        if (holdCount == 0) {
            owner = null;
            notify();
        }
    }

    public Condition newCondition() {
        return new Condition() {
            public void await() throws InterruptedException {}
            public void awaitUninterruptibly() {}
            public long awaitNanos(long nanosTimeout) { return 0; }
            public boolean await(long time, TimeUnit unit) { return true; }
            public boolean awaitUntil(Date deadline) { return true; }
            public void signal() {}
            public void signalAll() {}
        };
    }

    public synchronized int getHoldCount() {
        return (Thread.currentThread() == owner) ? holdCount : 0;
    }

    public synchronized boolean isHeldByCurrentThread() {
        return Thread.currentThread() == owner;
    }

    public synchronized boolean isLocked() {
        return owner != null;
    }
}
