package android.util;

/**
 * Base class for checked exceptions the Android framework declares.
 *
 * Needed as the parent of android.os.RemoteException so that class is a real
 * Throwable; apps also catch this type directly to handle any framework failure in
 * one place.
 */
public class AndroidException extends Exception {

    public AndroidException() {
        super();
    }

    public AndroidException(String name) {
        super(name);
    }

    public AndroidException(String name, Throwable cause) {
        super(name, cause);
    }

    public AndroidException(Exception cause) {
        super(cause);
    }
}
