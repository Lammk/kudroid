package android.app;

import android.app.Notification;

/**
 * mô phỏng android.app.notificationmanager.
 *
 * không quan trọng đối với việc khởi động/hiển thị ứng dụng. thông báo là no-op trên ios.
 */
public class NotificationManager {
    /** tầm quan trọng: không có. */
    public static final int IMPORTANCE_NONE = 0;
    /** tầm quan trọng: thấp. */
    public static final int IMPORTANCE_LOW = 2;
    /** tầm quan trọng: mặc định. */
    public static final int IMPORTANCE_DEFAULT = 3;
    /** tầm quan trọng: cao. */
    public static final int IMPORTANCE_HIGH = 4;

    public NotificationManager() {
    }

    /**
     * đăng một thông báo (no-op).
     */
    public void notify(int id, Notification notification) {
    }

    /**
     * đăng một thông báo có thẻ (no-op).
     */
    public void notify(String tag, int id, Notification notification) {
    }

    /**
     * hủy một thông báo (no-op).
     */
    public void cancel(int id) {
    }

    /**
     * hủy một thông báo có thẻ (no-op).
     */
    public void cancel(String tag, int id) {
    }

    /**
     * hủy tất cả thông báo (no-op).
     */
    public void cancelAll() {
    }

    /**
     * trả về tầm quan trọng hiện tại (no-op).
     */
    public int getImportance() {
        return IMPORTANCE_NONE;
    }
}