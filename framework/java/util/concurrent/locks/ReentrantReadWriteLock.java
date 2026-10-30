package java.util.concurrent.locks;

import java.io.Serializable;
import java.util.concurrent.TimeUnit;

/**
 * One reentrant lock backs both views: readers exclude each other, which is stricter than
 * a real read/write lock but never lets a reader see a writer's partial state.
 */
public class ReentrantReadWriteLock implements ReadWriteLock, Serializable {
    private static final long serialVersionUID = -6992448646407690164L;
    private final ReentrantLock lock = new ReentrantLock();
    private final ReadLock readerLock = new ReadLock(this);
    private final WriteLock writerLock = new WriteLock(this);
    private int readHolds;

    public ReentrantReadWriteLock() {}
    public ReentrantReadWriteLock(boolean fair) {}

    public ReentrantReadWriteLock.ReadLock readLock() { return readerLock; }
    public ReentrantReadWriteLock.WriteLock writeLock() { return writerLock; }

    public final boolean isFair() { return false; }
    public synchronized int getReadLockCount() { return readHolds; }
    public int getReadHoldCount() { return readHolds > 0 ? lock.getHoldCount() : 0; }
    public int getWriteHoldCount() { return isWriteLockedByCurrentThread() ? lock.getHoldCount() : 0; }
    public boolean isWriteLocked() { return lock.isLocked() && readHolds == 0; }
    public boolean isWriteLockedByCurrentThread() {
        return lock.isHeldByCurrentThread() && readHolds == 0;
    }

    private synchronized void readAcquired() { readHolds++; }
    private synchronized void readReleased() { if (readHolds > 0) readHolds--; }

    public static class ReadLock implements Lock, Serializable {
        private static final long serialVersionUID = -5992448646407690164L;
        private final ReentrantReadWriteLock owner;

        protected ReadLock(ReentrantReadWriteLock lock) { owner = lock; }

        public void lock() { owner.lock.lock(); owner.readAcquired(); }
        public void lockInterruptibly() throws InterruptedException {
            owner.lock.lockInterruptibly();
            owner.readAcquired();
        }
        public boolean tryLock() {
            if (!owner.lock.tryLock()) return false;
            owner.readAcquired();
            return true;
        }
        public boolean tryLock(long timeout, TimeUnit unit) throws InterruptedException {
            if (!owner.lock.tryLock(timeout, unit)) return false;
            owner.readAcquired();
            return true;
        }
        public void unlock() { owner.readReleased(); owner.lock.unlock(); }
        public Condition newCondition() { throw new UnsupportedOperationException(); }
    }

    public static class WriteLock implements Lock, Serializable {
        private static final long serialVersionUID = -4992448646407690164L;
        private final ReentrantReadWriteLock owner;

        protected WriteLock(ReentrantReadWriteLock lock) { owner = lock; }

        public void lock() { owner.lock.lock(); }
        public void lockInterruptibly() throws InterruptedException { owner.lock.lockInterruptibly(); }
        public boolean tryLock() { return owner.lock.tryLock(); }
        public boolean tryLock(long timeout, TimeUnit unit) throws InterruptedException {
            return owner.lock.tryLock(timeout, unit);
        }
        public void unlock() { owner.lock.unlock(); }
        public Condition newCondition() { return owner.lock.newCondition(); }
        public boolean isHeldByCurrentThread() { return owner.lock.isHeldByCurrentThread(); }
        public int getHoldCount() { return owner.lock.getHoldCount(); }
    }
}
