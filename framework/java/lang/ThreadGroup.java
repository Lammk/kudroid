package java.lang;

public class ThreadGroup {

    private final String name;

    public ThreadGroup(String name) {
        this.name = name;
    }

    public ThreadGroup(ThreadGroup parent, String name) {
        this.name = name;
    }

    public final String getName() {
        return name;
    }

    public final ThreadGroup getParent() {
        return null;
    }

    public int activeCount() {
        return 1;
    }

    public void uncaughtException(Thread t, Throwable e) {
        e.printStackTrace();
    }

    public String toString() {
        return "ThreadGroup[" + name + "]";
    }
}
