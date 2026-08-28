package java.util.concurrent;

public class CountDownLatch {
    private int count;

    public CountDownLatch(int count) {
        if (count < 0) throw new IllegalArgumentException("count < 0");
        this.count = count;
    }

    public synchronized void await() throws InterruptedException {
        while (count > 0) {
            wait();
        }
    }

    public synchronized boolean await(long timeout, TimeUnit unit) throws InterruptedException {
        long millis = unit.toMillis(timeout);
        if (millis <= 0) return count == 0;
        long deadline = System.currentTimeMillis() + millis;
        while (count > 0) {
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0) return false;
            wait(remaining);
        }
        return true;
    }

    public synchronized void countDown() {
        if (count > 0) {
            count--;
            if (count == 0) {
                notifyAll();
            }
        }
    }

    public synchronized long getCount() {
        return count;
    }

    public String toString() {
        return super.toString() + "[Count = " + getCount() + "]";
    }
}
