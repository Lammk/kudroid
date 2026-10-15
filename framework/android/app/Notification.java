package android.app;

import android.content.Context;
import android.content.Intent;

/**
 * simulate android.app.notification.
 *
 * represents a notification. for kudroid minimal framework, here is an emulation
 * stores basic fields.
 */
public class Notification {
    /** notification priority: default. */
    public static final int PRIORITY_DEFAULT = 0;
    /** notification priority: low. */
    public static final int PRIORITY_LOW = -1;
    /** notification priority: high. */
    public static final int PRIORITY_HIGH = 1;

    /** notification icon. */
    public int icon;
    /** notification text. */
    public CharSequence tickerText;
    /** notification content title. */
    public CharSequence contentTitle;
    /** notification content text. */
    public CharSequence contentText;
    /** notification content intent. */
    public PendingIntent contentIntent;
    /** notification priority. */
    public int priority = PRIORITY_DEFAULT;

    public Notification() {
    }

    public Notification(int icon, CharSequence tickerText, long when) {
        this.icon = icon;
        this.tickerText = tickerText;
    }

    /**
     * generator to create notifications.
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

    public static class Action {
        public Action() {}
    }

    public static class BigPictureStyle {
        public BigPictureStyle() {}
    }

    public static class BubbleMetadata {
        public BubbleMetadata() {}
    }

    public static class CallStyle {
        public CallStyle() {}
    }

    public static class MessagingStyle {
        public MessagingStyle() {}
    }

    public static class Message {
        public Message() {}
    }

    public static class Style {
        public Style() {}
    }

}