package android.net;

/**
 * Stub android.net.NetworkCapabilities.
 *
 * Describes the capabilities of a network. For KuDroid's minimal framework,
 * this is a stub.
 */
public class NetworkCapabilities {
    /** Network capability: internet. */
    public static final int NET_CAPABILITY_INTERNET = 12;
    /** Network capability: validated. */
    public static final int NET_CAPABILITY_VALIDATED = 16;
    /** Network capability: not metered. */
    public static final int NET_CAPABILITY_NOT_METERED = 11;

    private int mCapabilities = 0;

    public NetworkCapabilities() {
    }

    public boolean hasCapability(int capability) {
        return (mCapabilities & (1 << capability)) != 0;
    }

    public void addCapability(int capability) {
        mCapabilities |= (1 << capability);
    }

    public void removeCapability(int capability) {
        mCapabilities &= ~(1 << capability);
    }
}