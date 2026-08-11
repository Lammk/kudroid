package android.app;

import android.content.Context;
import android.content.ContextWrapper;

/**
 * triển khai android.app.contextthemewrapper tối thiểu.
 *
 * một contextwrapper cho phép sửa đổi chủ đề. đối với khuôn khổ
 * tối thiểu của kudroid, đây là một lớp bọc mỏng xung quanh contextwrapper.
 */
public class ContextThemeWrapper extends ContextWrapper {
    public ContextThemeWrapper() {
        super(null);
    }

    public ContextThemeWrapper(Context base) {
        super(base);
    }

    public ContextThemeWrapper(Context base, int themeResId) {
        super(base);
    }
}
