package android.net;

public class ConnectivityManager {
    public static final int TYPE_MOBILE = 0;
    public static final int TYPE_WIFI = 1;
    public static final int TYPE_ETHERNET = 9;

    public ConnectivityManager() {}
    public NetworkInfo getActiveNetworkInfo() { return new NetworkInfo(TYPE_WIFI, 0, "WIFI", ""); }
    public NetworkInfo getNetworkInfo(int networkType) { return new NetworkInfo(networkType, 0, "WIFI", ""); }
    public Network getActiveNetwork() { return new Network(); }
    public NetworkCapabilities getNetworkCapabilities(Network network) { return new NetworkCapabilities(); }
    public Network[] getAllNetworks() { return new Network[]{ new Network() }; }
}
