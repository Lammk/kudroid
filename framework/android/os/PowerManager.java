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
        private static native void setKeepScreenOnNative(boolean keepOn);
        private boolean mHeld = false;

        public void acquire() {
            mHeld = true;
            try {
                setKeepScreenOnNative(true);
            } catch (Throwable ignored) {}
        }

        public void acquire(long timeout) {
            acquire();
        }

        public void release() {
            mHeld = false;
            try {
                setKeepScreenOnNative(false);
            } catch (Throwable ignored) {}
        }

        public void release(int flags) {
            release();
        }

        public boolean isHeld() {
            return mHeld;
        }

        public void setReferenceCounted(boolean value) {
        }
    }
}