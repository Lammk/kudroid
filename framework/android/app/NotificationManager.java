package android.app;

import java.util.List;
import java.util.Collections;

public class NotificationManager {
    public static final int IMPORTANCE_UNSPECIFIED = -1000;
    public static final int IMPORTANCE_NONE = 0;
    public static final int IMPORTANCE_MIN = 1;
    public static final int IMPORTANCE_LOW = 2;
    public static final int IMPORTANCE_DEFAULT = 3;
    public static final int IMPORTANCE_HIGH = 4;
    public static final int IMPORTANCE_MAX = 5;

    public NotificationManager() {}
    public void notify(int id, Notification notification) {}
    public void notify(String tag, int id, Notification notification) {}
    public void cancel(int id) {}
    public void cancel(String tag, int id) {}
    public void cancelAll() {}
    public void createNotificationChannel(NotificationChannel channel) {}
    public void createNotificationChannels(List<NotificationChannel> channels) {}
    public NotificationChannel getNotificationChannel(String channelId) { return null; }
    public List<NotificationChannel> getNotificationChannels() { return Collections.emptyList(); }
    public void deleteNotificationChannel(String channelId) {}
}
