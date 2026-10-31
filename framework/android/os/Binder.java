package android.os;

public class Binder implements IBinder {
    private IInterface mOwner;
    private String mDescriptor;

    public Binder() {}

    public static int getCallingUid() { return Process.myUid(); }

    public void attachInterface(IInterface owner, String descriptor) {
        mOwner = owner;
        mDescriptor = descriptor;
    }

    public String getInterfaceDescriptor() { return mDescriptor; }
    public boolean isBinderAlive() { return true; }
    public boolean pingBinder() { return true; }

    public IInterface queryLocalInterface(String descriptor) {
        return mDescriptor != null && mDescriptor.equals(descriptor) ? mOwner : null;
    }

    public final boolean transact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        if (data != null) data.setDataPosition(0);
        boolean handled = onTransact(code, data, reply, flags);
        if (reply != null) reply.setDataPosition(0);
        return handled;
    }

    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        if (code == INTERFACE_TRANSACTION) {
            if (reply != null) reply.writeString(getInterfaceDescriptor());
            return true;
        }
        return false;
    }

    // Local binders live for the duration of their process.
    public void linkToDeath(DeathRecipient recipient, int flags) {}
    public boolean unlinkToDeath(DeathRecipient recipient, int flags) { return true; }
}
