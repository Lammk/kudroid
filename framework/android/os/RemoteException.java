package android.os;

/**
 * android.os.RemoteException.
 *
 * The generated stub did not extend anything, so it was not a Throwable: code could
 * not throw it and, worse, could not declare it in a throws clause — every framework
 * signature that mentions it failed to compile. Any app calling a method declared to
 * throw RemoteException (Messenger.send, every IBinder transaction, all AIDL-derived
 * interfaces) had no working type to catch.
 *
 * Extends android.util.AndroidException, matching AOSP, so an app catching that base
 * type behaves as it would on a device.
 */
public class RemoteException extends android.util.AndroidException {

    public RemoteException() {
        super();
    }

    public RemoteException(String message) {
        super(message);
    }

    public RemoteException(String message, Throwable cause) {
        super(message, cause);
    }
}
