package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.net.Socket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;
import java.net.URL;
import java.net.URLConnection;
import java.io.IOException;

public class Network implements Parcelable {
    private final int mNetId;

    public Network() { this(0); }

    Network(int netId) { mNetId = netId; }

    public long getNetworkHandle() { return ((long) mNetId << 32) | 0xfaceL; }
    private void requireActive() throws SocketException {
        Network active = new ConnectivityManager().getActiveNetwork();
        if (!equals(active)) throw new SocketException("Network is unavailable");
    }
    public void bindSocket(Socket socket) throws IOException {
        if (socket == null) throw new NullPointerException("socket");
        requireActive();
    }
    public void bindSocket(DatagramSocket socket) throws IOException {
        if (socket == null) throw new NullPointerException("socket");
        requireActive();
    }
    public InetAddress[] getAllByName(String host) throws java.net.UnknownHostException { return InetAddress.getAllByName(host); }
    public InetAddress getByName(String host) throws java.net.UnknownHostException { return InetAddress.getByName(host); }
    public javax.net.SocketFactory getSocketFactory() { return javax.net.SocketFactory.getDefault(); }
    public URLConnection openConnection(URL url) throws IOException { return url.openConnection(); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}

    @Override public boolean equals(Object obj) {
        return obj instanceof Network && ((Network) obj).mNetId == mNetId;
    }

    @Override public int hashCode() { return mNetId * 11; }
    @Override public String toString() { return Integer.toString(mNetId); }
}
