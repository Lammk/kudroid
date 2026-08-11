package android.os;

/**
 * mô phỏng android.os.powermanager.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về giá trị mặc định để các ứng dụng
 * không gặp sự cố khi chúng truy vấn trạng thái nguồn.
 */
public class PowerManager {
    /** cờ khóa thức: một phần. */
    public static final int PARTIAL_WAKE_LOCK = 1;
    /** cờ khóa thức: màn hình mờ. */
    public static final int SCREEN_DIM_WAKE_LOCK = 6;
    /** cờ khóa thức: màn hình sáng. */
    public static final int SCREEN_BRIGHT_WAKE_LOCK = 10;
    /** cờ khóa thức: đầy đủ. */
    public static final int FULL_WAKE_LOCK = 26;

    public PowerManager() {
    }

    public boolean isScreenOn() {
        return true;
    }

    public boolean isInteractive() {
        return true;
    }

    public WakeLock newWakeLock(int flags, String tag) {
        return new WakeLock();
    }

    public void wakeUp(long time) {
    }

    public void goToSleep(long time) {
    }

    /**
     * mô phỏng wakelock.
     */
    public static class WakeLock {
        public void acquire() {
        }

        public void acquire(long timeout) {
        }

        public void release() {
        }

        public void release(int flags) {
        }

        public boolean isHeld() {
            return false;
        }

        public void setReferenceCounted(boolean value) {
        }
    }
}