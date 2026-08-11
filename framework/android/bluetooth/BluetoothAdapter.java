package android.bluetooth;

/**
 * Stub android.bluetooth.BluetoothAdapter.
 *
 * Non-critical for app startup/rendering. Returns null/defaults so apps don't
 * crash when they check Bluetooth availability.
 */
public final class BluetoothAdapter {
    /** State: off. */
    public static final int STATE_OFF = 10;
    /** State: on. */
    public static final int STATE_ON = 12;

    private static final BluetoothAdapter sInstance = new BluetoothAdapter();

    private BluetoothAdapter() {
    }

    /**
     * Return the default adapter. Returns null (no Bluetooth on iOS).
     */
    public static BluetoothAdapter getDefaultAdapter() {
        return null;
    }

    public boolean isEnabled() {
        return false;
    }

    public int getState() {
        return STATE_OFF;
    }

    public String getName() {
        return null;
    }

    public String getAddress() {
        return null;
    }

    public boolean enable() {
        return false;
    }

    public boolean disable() {
        return false;
    }
}