package android.view.inputmethod;

/**
 * mô phỏng android.view.inputmethod.inputmethodmanager.
 *
 * quản lý phương thức nhập (bàn phím). đối với khuôn khổ tối thiểu của kudroid, đây
 * là một mô phỏng.
 */
public class InputMethodManager {
    /** kết quả: thành công. */
    public static final int RESULT_SUCCESS = 0;
    /** kết quả: được hiển thị. */
    public static final int RESULT_SHOWN = 1;
    /** kết quả: bị ẩn. */
    public static final int RESULT_HIDDEN = 2;

    public InputMethodManager() {
    }

    public boolean showSoftInput(android.view.View view, int flags) {
        return false;
    }

    public boolean hideSoftInputFromWindow(android.os.IBinder windowToken, int flags) {
        return false;
    }

    public void toggleSoftInputFromWindow(android.os.IBinder windowToken, int showFlags, int hideFlags) {
    }

    public boolean isAcceptingText() {
        return false;
    }

    public boolean isActive() {
        return false;
    }
}