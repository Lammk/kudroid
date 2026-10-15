package java.util;

public class Timer {
    private final String name;
    public Timer() { this("Timer-" + (++serialNumber)); }
    public Timer(boolean isDaemon) { this("Timer-" + (++serialNumber)); }
    public Timer(String name) { this.name = name; }
    public Timer(String name, boolean isDaemon) { this.name = name; }

    private static int serialNumber = 0;

    public void schedule(TimerTask task, long delay) {
        if (task != null) task.run();
    }
    public void schedule(TimerTask task, Date time) {
        if (task != null) task.run();
    }
    public void schedule(TimerTask task, long delay, long period) {
        if (task != null) task.run();
    }
    public void schedule(TimerTask task, Date firstTime, long period) {
        if (task != null) task.run();
    }
    public void scheduleAtFixedRate(TimerTask task, long delay, long period) {
        if (task != null) task.run();
    }
    public void scheduleAtFixedRate(TimerTask task, Date firstTime, long period) {
        if (task != null) task.run();
    }
    public void cancel() {}
    public int purge() { return 0; }
}
