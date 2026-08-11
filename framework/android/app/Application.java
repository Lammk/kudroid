package android.app;

import android.content.Context;
import android.content.ContextWrapper;

/**
 * triển khai android.app.application tối thiểu.
 *
 * lớp cơ sở cho ứng dụng. đối với khuôn khổ tối thiểu của kudroid, lớp này
 * cung cấp lệnh gọi lại vòng đời oncreate.
 */
public class Application extends ContextWrapper {
    public Application() {
        super(null);
    }

    /**
     * được gọi khi ứng dụng đang khởi động.
     */
    public void onCreate() {
    }

    /**
     * được gọi khi ứng dụng sắp hết bộ nhớ.
     */
    public void onLowMemory() {
    }

    /**
     * được gọi khi ứng dụng bị cắt giảm bộ nhớ.
     */
    public void onTrimMemory(int level) {
    }

    /**
     * đính kèm bối cảnh cơ sở (được gọi bởi khuôn khổ).
     */
    public void attach(Context base) {
        attachBaseContext(base);
    }
}
