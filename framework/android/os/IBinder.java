package android.os;

/**
 * mô phỏng android.os.ibinder.
 *
 * một giao diện cơ bản cho một đối tượng từ xa. đối với khuôn khổ tối thiểu của kudroid,
 * đây là một mô phỏng.
 */
public interface IBinder {
    /** Mã transaction đầu tiên dành cho app. */
    public static final int FIRST_CALL_TRANSACTION = 0x00000001;
    /** Mã transaction cuối cùng dành cho app. */
    public static final int LAST_CALL_TRANSACTION = 0x00ffffff;
    /** Cờ transact: một chiều, không chờ reply. */
    public static final int FLAG_ONEWAY = 0x00000001;

    /**
     * Callback khi đầu bên kia chết. Mọi binder của KuDroid nằm cùng process nên
     * chỉ kích hoạt khi binder bị dispose tường minh.
     */
    public interface DeathRecipient {
        void binderDied();
    }

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

    boolean transact(int code, Parcel data, Parcel reply, int flags);

    void linkToDeath(DeathRecipient recipient, int flags);

    boolean unlinkToDeath(DeathRecipient recipient, int flags);

    IInterface queryLocalInterface(String descriptor);
}