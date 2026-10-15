package android.os;

/**
 * emulate android.os.ibinder.
 *
 * a basic interface to a remote object. for kudroid minimal framework,
 *This is a simulation.
 */
public interface IBinder {
    /** First transaction code for app. */
    public static final int FIRST_CALL_TRANSACTION = 0x00000001;
    /** Final transaction code for app. */
    public static final int LAST_CALL_TRANSACTION = 0x00ffffff;
    /** Transact flag: one-way, no reply waiting. */
    public static final int FLAG_ONEWAY = 0x00000001;

    /**
     * Callback when the other end dies. All KuDroid binders are in the same process
     * only fires when the binder is explicitly disposed.
     */
    public interface DeathRecipient {
        void binderDied();
    }

    /**
     * returns a string representation of the binder.
     */
    String getInterfaceDescriptor();

    /**
     * returns whether the binder is alive or not.
     */
    boolean isBinderAlive();

    /**
     * returns whether the binder is currently transacting or not.
     */
    boolean pingBinder();

    boolean transact(int code, Parcel data, Parcel reply, int flags);

    void linkToDeath(DeathRecipient recipient, int flags);

    boolean unlinkToDeath(DeathRecipient recipient, int flags);

    IInterface queryLocalInterface(String descriptor);
}