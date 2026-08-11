package android.os;

/**
 * mô phỏng android.os.vibrator.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. rung là một thao tác không hoạt động (no-op) trên ios.
 */
public class Vibrator {
    public Vibrator() {
    }

    public boolean hasVibrator() {
        return false;
    }

    public void vibrate(long milliseconds) {
    }

    public void vibrate(long[] pattern, int repeat) {
    }

    public void vibrate(android.os.VibrationEffect effect) {
    }

    public void cancel() {
    }
}