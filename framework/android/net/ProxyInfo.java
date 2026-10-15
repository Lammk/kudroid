package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public class ProxyInfo implements Parcelable {
    private String mHost;
    private int mPort;

    public ProxyInfo(String host, int port) {
        mHost = host;
        mPort = port;
    }

    public static ProxyInfo buildDirectProxy(String host, int port) {
        return new ProxyInfo(host, port);
    }

    public String getHost() { return mHost; }
    public int getPort() { return mPort; }

    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
