package android.bluetooth;

import java.util.Set;
import java.util.Collections;

public final class BluetoothAdapter {
    public static final String ACTION_STATE_CHANGED = "android.bluetooth.adapter.action.STATE_CHANGED";
    public static final int STATE_OFF = 10;
    public static final int STATE_ON = 12;

    private static BluetoothAdapter sAdapter;

    private BluetoothAdapter() {}
    public static synchronized BluetoothAdapter getDefaultAdapter() {
        if (sAdapter == null) sAdapter = new BluetoothAdapter();
        return sAdapter;
    }
    public boolean isEnabled() { return true; }
    public int getState() { return STATE_ON; }
    public String getName() { return "KuDroid Device"; }
    public String getAddress() { return "02:00:00:00:00:00"; }
    public Set<BluetoothDevice> getBondedDevices() { return Collections.emptySet(); }
    public boolean startDiscovery() { return true; }
    public boolean cancelDiscovery() { return true; }
    public boolean isDiscovering() { return false; }
    public BluetoothDevice getRemoteDevice(String address) { return new BluetoothDevice(address); }
    public BluetoothDevice getRemoteDevice(byte[] address) { return new BluetoothDevice("02:00:00:00:00:00"); }
}
