package android.bluetooth;

/**
 * emulate android.bluetooth.bluetoothadapter.
 *
 * is not important for application startup/display. returns null/default so the application doesn't
 * had trouble checking bluetooth availability.
 */
public final class BluetoothAdapter {
    /** status: off. */
    public static final int STATE_OFF = 10;
    /** status: enabled. */
    public static final int STATE_ON = 12;

    private static final BluetoothAdapter sInstance = new BluetoothAdapter();

    private BluetoothAdapter() {
    }

    /**
     * returns default adapter. returns null (no bluetooth on ios).
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