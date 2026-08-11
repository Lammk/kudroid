package android.content;

/**
 * triển khai android.content.dialoginterface tối thiểu.
 *
 * giao diện cho các hộp thoại. đối với khuôn khổ tối thiểu của kudroid, cung cấp hằng số
 * nút và trình lắng nghe nhấp chuột.
 */
public interface DialogInterface {
    /** nút: tích cực. */
    public static final int BUTTON_POSITIVE = -1;
    /** nút: tiêu cực. */
    public static final int BUTTON_NEGATIVE = -2;
    /** nút: trung lập. */
    public static final int BUTTON_NEUTRAL = -3;

    /**
     * giao diện cho các cuộc gọi lại khi nhấp chuột.
     */
    public interface OnClickListener {
        void onClick(DialogInterface dialog, int which);
    }

    /**
     * giao diện cho các cuộc gọi lại khi bỏ qua.
     */
    public interface OnDismissListener {
        void onDismiss(DialogInterface dialog);
    }

    /**
     * giao diện cho các cuộc gọi lại khi hủy.
     */
    public interface OnCancelListener {
        void onCancel(DialogInterface dialog);
    }

    /**
     * bỏ qua hộp thoại.
     */
    void dismiss();

    /**
     * hủy hộp thoại.
     */
    void cancel();
}
