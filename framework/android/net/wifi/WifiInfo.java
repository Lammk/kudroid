package android.net.wifi;

import android.os.Parcel;
import android.os.Parcelable;

public class WifiInfo implements Parcelable {
    public static final String LINK_SPEED_UNITS = "Mbps";
    public WifiInfo() {}
    public String getSSID() { return "\"KuDroid_WiFi\""; }
    public String getBSSID() { return "00:11:22:33:44:55"; }
    public int getRssi() { return -50; }
    public int getLinkSpeed() { return 100; }
    public int getIpAddress() { return 0x0100007F; } // 127.0.0.1
    public String getMacAddress() { return "02:00:00:00:00:00"; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
