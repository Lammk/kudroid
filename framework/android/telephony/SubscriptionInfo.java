package android.telephony;

import android.os.Parcel;
import android.os.Parcelable;

public class SubscriptionInfo implements Parcelable {
    private int mId = 1;
    private String mIccId = "89012600000000000000";
    private CharSequence mDisplayName = "Carrier";
    private CharSequence mCarrierName = "KuDroid Telecom";

    public SubscriptionInfo() {}
    public int getSubscriptionId() { return mId; }
    public String getIccId() { return mIccId; }
    public CharSequence getDisplayName() { return mDisplayName; }
    public CharSequence getCarrierName() { return mCarrierName; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
