package android.net.wifi;

/**
 * Stub android.net.wifi.WifiInfo.
 *
 * Describes the state of a WiFi connection. For KuDroid's minimal framework,
 * this is a stub with defaults.
 */
public class WifiInfo {
    private String mSSID = "";
    private String mBSSID = "";
    private int mIpAddress = 0;
    private int mRssi = -127;
    private int mLinkSpeed = 0;

    public WifiInfo() {
    }

    public String getSSID() {
        return mSSID;
    }

    public String getBSSID() {
        return mBSSID;
    }

    public int getIpAddress() {
        return mIpAddress;
    }

    public int getRssi() {
        return mRssi;
    }

    public int getLinkSpeed() {
        return mLinkSpeed;
    }

    public boolean isHiddenSSID() {
        return false;
    }

    public String getMacAddress() {
        return null;
    }
}