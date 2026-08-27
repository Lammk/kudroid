package java.lang;

/**
 * Gốc của mọi class. KuART cấp phát object theo layout DexObject nên class này
 * không được khai báo field instance nào.
 */
public class Object {

    public Object() {
    }

    public native Class<?> getClass();

    public native int hashCode();

    public boolean equals(Object obj) {
        return this == obj;
    }

    protected native Object clone() throws CloneNotSupportedException;

    public String toString() {
        return getClass().getName() + "@" + Integer.toHexString(hashCode());
    }

    public native void notify();

    public native void notifyAll();

    public native void wait() throws InterruptedException;

    public native void wait(long millis) throws InterruptedException;

    public void wait(long millis, int nanos) throws InterruptedException {
        wait(millis);
    }

    protected void finalize() throws Throwable {
    }
}
