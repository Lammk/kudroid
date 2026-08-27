package android.net;

/**
 * emulate android.net.network.
 *
 * represents a network. for kudroid minimal framework, here is an emulation.
 */
public class Network {
    private final int mNetId;

    public Network(int netId) {
        mNetId = netId;
    }

    public int getNetId() {
        return mNetId;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Network)) return false;
        return mNetId == ((Network) o).mNetId;
    }

    @Override
    public int hashCode() {
        return mNetId;
    }
}