package java.lang;

public class ThreadLocal<T> {

    private final java.util.HashMap<Thread, T> values = new java.util.HashMap<Thread, T>();

    public T get() {
        Thread t = Thread.currentThread();
        synchronized (values) {
            if (!values.containsKey(t)) {
                T initial = initialValue();
                values.put(t, initial);
                return initial;
            }
            return values.get(t);
        }
    }

    public void set(T value) {
        synchronized (values) {
            values.put(Thread.currentThread(), value);
        }
    }

    public void remove() {
        synchronized (values) {
            values.remove(Thread.currentThread());
        }
    }

    protected T initialValue() {
        return null;
    }
}
