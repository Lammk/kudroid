package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.net.InetAddress;

public class LinkAddress implements Parcelable {
    private final InetAddress address;
    private final int prefixLength;

    public LinkAddress(InetAddress address, int prefixLength) {
        this.address = address;
        this.prefixLength = prefixLength;
    }
    public InetAddress getAddress() { return address; }
    public int getPrefixLength() { return prefixLength; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
