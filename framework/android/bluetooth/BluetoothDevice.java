package android.bluetooth;

import android.os.Parcel;
import android.os.Parcelable;

public final class BluetoothDevice implements Parcelable {
    public static final int BOND_NONE = 10;
    public static final int BOND_BONDED = 12;
    private final String mAddress;

    BluetoothDevice(String address) { this.mAddress = address; }
    public String getAddress() { return mAddress; }
    public String getName() { return "Bluetooth Gamepad"; }
    public int getBondState() { return BOND_BONDED; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) {}
}
