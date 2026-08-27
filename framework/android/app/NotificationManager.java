package android.app;

import android.app.Notification;

/**
 * emulate android.app.notificationmanager.
 *
 * is not important for application startup/display. notification is no-op on ios.
 */
public class NotificationManager {
    /** importance: none. */
    public static final int IMPORTANCE_NONE = 0;
    /** importance: low. */
    public static final int IMPORTANCE_LOW = 2;
    /** importance: default. */
    public static final int IMPORTANCE_DEFAULT = 3;
    /** importance: high. */
    public static final int IMPORTANCE_HIGH = 4;

    public NotificationManager() {
    }

    /**
     * post a notice (no-op).
     */
    public void notify(int id, Notification notification) {
    }

    /**
     * post a message with tag (no-op).
     */
    public void notify(String tag, int id, Notification notification) {
    }

    /**
     * cancel a notification (no-op).
     */
    public void cancel(int id) {
    }

    /**
     * cancel a message with a tag (no-op).
     */
    public void cancel(String tag, int id) {
    }

    /**
     * cancel all notifications (no-op).
     */
    public void cancelAll() {
    }

    /**
     * returns the current importance (no-op).
     */
    public int getImportance() {
        return IMPORTANCE_NONE;
    }
}