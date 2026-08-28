package android.app;

public class AlarmManager {
    public static final int RTC_WAKEUP = 0;
    public static final int RTC = 1;
    public static final int ELAPSED_REALTIME_WAKEUP = 2;
    public static final int ELAPSED_REALTIME = 3;

    public AlarmManager() {}
    public void set(int type, long triggerAtMillis, PendingIntent operation) {}
    public void setExact(int type, long triggerAtMillis, PendingIntent operation) {}
    public void setRepeating(int type, long triggerAtMillis, long intervalMillis, PendingIntent operation) {}
    public void cancel(PendingIntent operation) {}
}
