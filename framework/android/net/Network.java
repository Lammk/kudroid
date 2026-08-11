package android.net;

/**
 * Stub android.net.Network.
 *
 * Represents a network. For KuDroid's minimal framework, this is a stub.
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