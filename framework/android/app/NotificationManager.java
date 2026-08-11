package android.app;

import android.app.Notification;

/**
 * Stub android.app.NotificationManager.
 *
 * Non-critical for app startup/rendering. Notifications are no-ops on iOS.
 */
public class NotificationManager {
    /** Importance: none. */
    public static final int IMPORTANCE_NONE = 0;
    /** Importance: low. */
    public static final int IMPORTANCE_LOW = 2;
    /** Importance: default. */
    public static final int IMPORTANCE_DEFAULT = 3;
    /** Importance: high. */
    public static final int IMPORTANCE_HIGH = 4;

    public NotificationManager() {
    }

    /**
     * Post a notification (no-op).
     */
    public void notify(int id, Notification notification) {
    }

    /**
     * Post a notification with a tag (no-op).
     */
    public void notify(String tag, int id, Notification notification) {
    }

    /**
     * Cancel a notification (no-op).
     */
    public void cancel(int id) {
    }

    /**
     * Cancel a notification with a tag (no-op).
     */
    public void cancel(String tag, int id) {
    }

    /**
     * Cancel all notifications (no-op).
     */
    public void cancelAll() {
    }

    /**
     * Return the current importance (no-op).
     */
    public int getImportance() {
        return IMPORTANCE_NONE;
    }
}