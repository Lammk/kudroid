package android.os;

/**
 * android.os.Messenger.
 *
 * Was a stub with only a no-arg constructor, so {@code new Messenger(handler)} —
 * the only form apps actually use — resolved to an auto-stubbed method that
 * returned without running. The object then looked valid while being unusable, and
 * the failure appeared later at send() with nothing pointing back here.
 *
 * Messenger is the client end of a Handler: send() posts to the Handler's queue.
 * Real Android marshals across process boundaries via IBinder, which does not apply
 * to KuDroid — everything runs in one process — so a local send is both correct and
 * complete for the in-process case, and the IBinder form is accepted but cannot
 * deliver.
 */
public final class Messenger implements Parcelable {

    private final Handler mHandler;
    private final IBinder mBinder;

    public Messenger(Handler target) {
        mHandler = target;
        mBinder = null;
    }

    /**
     * Remote form.
     *
     * KuDroid has a single process and no Binder transport, so there is nothing to
     * deliver to. Kept so code that unparcels a Messenger still constructs.
     */
    public Messenger(IBinder target) {
        mHandler = null;
        mBinder = target;
    }

    /**
     * Deliver a message to the target Handler.
     *
     * @throws RemoteException when this Messenger has no local Handler, which is
     *         what a caller is required to handle and what Android throws once the
     *         far end is gone.
     */
    public void send(Message message) throws RemoteException {
        if (mHandler == null) {
            throw new RemoteException("Messenger has no local Handler (no Binder transport)");
        }
        if (message == null) return;
        mHandler.sendMessage(message);
    }

    public IBinder getBinder() {
        return mBinder;
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof Messenger)) return false;
        final Messenger o = (Messenger) other;
        if (mHandler != null) return mHandler == o.mHandler;
        return mBinder != null && mBinder == o.mBinder;
    }

    @Override
    public int hashCode() {
        if (mHandler != null) return mHandler.hashCode();
        return mBinder != null ? mBinder.hashCode() : 0;
    }

    public int describeContents() { return 0; }

    public void writeToParcel(Parcel out, int flags) {}

    public static Messenger readMessengerOrNullFromParcel(Parcel in) { return null; }

    public static void writeMessengerOrNullToParcel(Messenger messenger, Parcel out) {}
}
