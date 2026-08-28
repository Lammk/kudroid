package java.util.concurrent.locks;

import java.io.Serializable;

public class ReentrantReadWriteLock implements ReadWriteLock, Serializable {
    private static final long serialVersionUID = -6992448646407690164L;
    private final ReentrantLock lock = new ReentrantLock();

    public ReentrantReadWriteLock() {}
    public ReentrantReadWriteLock(boolean fair) {}

    public Lock readLock() { return lock; }
    public Lock writeLock() { return lock; }
}
