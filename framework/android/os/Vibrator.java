package android.os;

/**
 * mô phỏng android.os.vibrator với cầu nối rung Haptic Feedback thực thụ trên iOS Taptic Engine.
 */
public class Vibrator {
    public Vibrator() {
    }

    public boolean hasVibrator() {
        return true;
    }

    public void vibrate(long milliseconds) {
        try {
            if (milliseconds <= 50) {
                kudroid_vibrate_native(1); // Rung nhẹ (Light Impact)
            } else if (milliseconds <= 200) {
                kudroid_vibrate_native(2); // Rung vừa (Medium Impact)
            } else {
                kudroid_vibrate_native(3); // Rung mạnh (Heavy Impact)
            }
        } catch (Throwable ignored) {}
    }

    public void vibrate(long[] pattern, int repeat) {
        vibrate(100);
    }

    public void vibrate(android.os.VibrationEffect effect) {
        vibrate(100);
    }

    public void cancel() {
    }

    private static native void kudroid_vibrate_native(int intensity);
}