package android.net;

/**
 * emulate android.net.networkcapabilities.
 *
 * describes the capabilities of a network. for kudroid minimal framework,
 *This is a simulation.
 */
public class NetworkCapabilities {
    /** network capabilities: internet. */
    public static final int NET_CAPABILITY_INTERNET = 12;
    /** network capabilities: authenticated. */
    public static final int NET_CAPABILITY_VALIDATED = 16;
    /** network capacity: not measured. */
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