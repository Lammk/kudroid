package android.app;

import android.content.Context;
import android.content.Intent;

/**
 * Stub android.app.Notification.
 *
 * Represents a notification. For KuDroid's minimal framework, this is a stub
 * that stores basic fields.
 */
public class Notification {
    /** Notification priority: default. */
    public static final int PRIORITY_DEFAULT = 0;
    /** Notification priority: low. */
    public static final int PRIORITY_LOW = -1;
    /** Notification priority: high. */
    public static final int PRIORITY_HIGH = 1;

    /** The notification icon. */
    public int icon;
    /** The notification text. */
    public CharSequence tickerText;
    /** The notification content title. */
    public CharSequence contentTitle;
    /** The notification content text. */
    public CharSequence contentText;
    /** The notification content intent. */
    public PendingIntent contentIntent;
    /** The notification priority. */
    public int priority = PRIORITY_DEFAULT;

    public Notification() {
    }

    public Notification(int icon, CharSequence tickerText, long when) {
        this.icon = icon;
        this.tickerText = tickerText;
    }

    /**
     * Builder for creating a Notification.
     */
    public static class Builder {
        private final Context mContext;
        private final Notification mNotification = new Notification();

        public Builder(Context context) {
            mContext = context;
        }

        public Builder setSmallIcon(int icon) {
            mNotification.icon = icon;
            return this;
        }

        public Builder setContentTitle(CharSequence title) {
            mNotification.contentTitle = title;
            return this;
        }

        public Builder setContentText(CharSequence text) {
            mNotification.contentText = text;
            return this;
        }

        public Builder setTicker(CharSequence tickerText) {
            mNotification.tickerText = tickerText;
            return this;
        }

        public Builder setContentIntent(PendingIntent intent) {
            mNotification.contentIntent = intent;
            return this;
        }

        public Builder setPriority(int priority) {
            mNotification.priority = priority;
            return this;
        }

        public Builder setAutoCancel(boolean autoCancel) {
            return this;
        }

        public Notification build() {
            return mNotification;
        }
    }
}