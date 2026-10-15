package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.net.Socket;
import java.net.URL;
import java.net.URLConnection;
import java.io.IOException;

public class Network implements Parcelable {
    private final int mNetId;

    public Network() { this(0); }

    Network(int netId) { mNetId = netId; }

    public long getNetworkHandle() { return ((long) mNetId << 32) | 0xfaceL; }
    public Socket bindSocket(Socket socket) throws IOException { return socket; }
    public URLConnection openConnection(URL url) throws IOException { return url.openConnection(); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}

    @Override public boolean equals(Object obj) {
        return obj instanceof Network && ((Network) obj).mNetId == mNetId;
    }

    @Override public int hashCode() { return mNetId * 11; }
    @Override public String toString() { return Integer.toString(mNetId); }
}
