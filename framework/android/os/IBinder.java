package android.os;

/**
 * Stub android.os.IBinder.
 *
 * A basic interface for a remote object. For KuDroid's minimal framework,
 * this is a stub.
 */
public interface IBinder {
    /**
     * Return a string representation of the binder.
     */
    String getInterfaceDescriptor();

    /**
     * Return whether the binder is alive.
     */
    boolean isBinderAlive();

    /**
     * Return whether the binder is transacting.
     */
    boolean pingBinder();
}