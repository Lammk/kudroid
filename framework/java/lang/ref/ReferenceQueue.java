package java.lang.ref;

public class ReferenceQueue<T> {
    public ReferenceQueue() {}

    public Reference<? extends T> poll() {
        return null;
    }

    public Reference<? extends T> remove(long timeout) throws IllegalArgumentException, InterruptedException {
        return null;
    }

    public Reference<? extends T> remove() throws InterruptedException {
        return remove(0);
    }
}
