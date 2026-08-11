package android.app;

import android.content.Context;
import android.content.Intent;

/**
 * mô phỏng android.app.notification.
 *
 * đại diện cho một thông báo. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng
 * lưu trữ các trường cơ bản.
 */
public class Notification {
    /** độ ưu tiên thông báo: mặc định. */
    public static final int PRIORITY_DEFAULT = 0;
    /** độ ưu tiên thông báo: thấp. */
    public static final int PRIORITY_LOW = -1;
    /** độ ưu tiên thông báo: cao. */
    public static final int PRIORITY_HIGH = 1;

    /** biểu tượng thông báo. */
    public int icon;
    /** văn bản thông báo. */
    public CharSequence tickerText;
    /** tiêu đề nội dung thông báo. */
    public CharSequence contentTitle;
    /** văn bản nội dung thông báo. */
    public CharSequence contentText;
    /** intent nội dung thông báo. */
    public PendingIntent contentIntent;
    /** độ ưu tiên thông báo. */
    public int priority = PRIORITY_DEFAULT;

    public Notification() {
    }

    public Notification(int icon, CharSequence tickerText, long when) {
        this.icon = icon;
        this.tickerText = tickerText;
    }

    /**
     * trình tạo để tạo notification.
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