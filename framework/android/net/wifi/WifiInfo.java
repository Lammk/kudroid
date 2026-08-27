package android.net.wifi;

/**
 * emulate android.net.wifi.wifiinfo.
 *
 * describes the status of the wifi connection. for kudroid minimal framework,
 *this is a simulation with default values.
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