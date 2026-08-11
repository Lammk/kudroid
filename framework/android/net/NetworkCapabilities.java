package android.net;

/**
 * mô phỏng android.net.networkcapabilities.
 *
 * mô tả các khả năng của một mạng. đối với khuôn khổ tối thiểu của kudroid,
 * đây là một mô phỏng.
 */
public class NetworkCapabilities {
    /** khả năng mạng: internet. */
    public static final int NET_CAPABILITY_INTERNET = 12;
    /** khả năng mạng: đã xác thực. */
    public static final int NET_CAPABILITY_VALIDATED = 16;
    /** khả năng mạng: không đo lường. */
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