package android.net;

/**
 * mô phỏng android.net.network.
 *
 * đại diện cho một mạng. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
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