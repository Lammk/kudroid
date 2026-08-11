package android.app;

import android.content.Context;
import android.content.Intent;

/**
 * mô phỏng android.app.pendingintent.
 *
 * một mã thông báo cấp cho ứng dụng khác quyền thực hiện một thao tác. đối với
 * khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
 */
public final class PendingIntent {
    /** cờ: một lần. */
    public static final int FLAG_ONE_SHOT = 1;
    /** cờ: không tạo. */
    public static final int FLAG_NO_CREATE = 2;
    /** cờ: hủy hiện tại. */
    public static final int FLAG_CANCEL_CURRENT = 4;
    /** cờ: cập nhật hiện tại. */
    public static final int FLAG_UPDATE_CURRENT = 8;

    private final Intent mIntent;

    private PendingIntent(Intent intent) {
        mIntent = intent;
    }

    /**
     * nhận intent chờ xử lý của hoạt động.
     */
    public static PendingIntent getActivity(Context context, int requestCode,
                                            Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * nhận intent chờ xử lý của chương trình phát sóng.
     */
    public static PendingIntent getBroadcast(Context context, int requestCode,
                                             Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * nhận intent chờ xử lý của dịch vụ.
     */
    public static PendingIntent getService(Context context, int requestCode,
                                           Intent intent, int flags) {
        return new PendingIntent(intent);
    }

    /**
     * trả về intent được bọc.
     */
    public Intent getIntent() {
        return mIntent;
    }

    /**
     * gửi intent chờ xử lý (no-op).
     */
    public void send() {
    }
}