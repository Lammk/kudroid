package android.net.wifi;

/**
 * mô phỏng android.net.wifi.wifiinfo.
 *
 * mô tả trạng thái của kết nối wifi. đối với khuôn khổ tối thiểu của kudroid,
 * đây là một mô phỏng có các giá trị mặc định.
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