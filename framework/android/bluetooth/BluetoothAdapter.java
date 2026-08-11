package android.bluetooth;

/**
 * mô phỏng android.bluetooth.bluetoothadapter.
 *
 * không quan trọng đối với việc khởi động/hiển thị ứng dụng. trả về null/mặc định để ứng dụng không
 * gặp sự cố khi kiểm tra tính khả dụng của bluetooth.
 */
public final class BluetoothAdapter {
    /** trạng thái: tắt. */
    public static final int STATE_OFF = 10;
    /** trạng thái: bật. */
    public static final int STATE_ON = 12;

    private static final BluetoothAdapter sInstance = new BluetoothAdapter();

    private BluetoothAdapter() {
    }

    /**
     * trả về bộ điều hợp mặc định. trả về null (không có bluetooth trên ios).
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