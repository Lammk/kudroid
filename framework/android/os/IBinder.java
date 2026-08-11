package android.os;

/**
 * mô phỏng android.os.ibinder.
 *
 * một giao diện cơ bản cho một đối tượng từ xa. đối với khuôn khổ tối thiểu của kudroid,
 * đây là một mô phỏng.
 */
public interface IBinder {
    /**
     * trả về một biểu diễn chuỗi của binder.
     */
    String getInterfaceDescriptor();

    /**
     * trả về việc binder còn sống hay không.
     */
    boolean isBinderAlive();

    /**
     * trả về việc binder có đang giao dịch hay không.
     */
    boolean pingBinder();
}