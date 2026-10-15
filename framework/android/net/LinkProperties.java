package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.net.InetAddress;
import java.util.List;
import java.util.ArrayList;
import java.util.Collections;

public final class LinkProperties implements Parcelable {
    private String mIfaceName = "wlan0";
    private final ArrayList<LinkAddress> mLinkAddresses = new ArrayList<LinkAddress>();
    private final ArrayList<InetAddress> mDnses = new ArrayList<InetAddress>();
    private final ArrayList<RouteInfo> mRoutes = new ArrayList<RouteInfo>();

    public LinkProperties() {
        try {
            mDnses.add(InetAddress.getByName("8.8.8.8"));
            mDnses.add(InetAddress.getByName("8.8.4.4"));
        } catch (Throwable ignored) {}
    }
    public void setInterfaceName(String iface) { mIfaceName = iface; }
    public String getInterfaceName() { return mIfaceName; }
    public List<LinkAddress> getLinkAddresses() { return Collections.unmodifiableList(mLinkAddresses); }
    public List<InetAddress> getDnsServers() { return Collections.unmodifiableList(mDnses); }
    public List<RouteInfo> getRoutes() { return Collections.unmodifiableList(mRoutes); }
    public ProxyInfo getHttpProxy() { return null; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
