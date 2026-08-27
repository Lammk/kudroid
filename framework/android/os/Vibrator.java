package android.os;

/**
 * simulates android.os.vibrator with real Haptic Feedback vibration bridge on iOS Taptic Engine.
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
                kudroid_vibrate_native(1); // Light Impact
            } else if (milliseconds <= 200) {
                kudroid_vibrate_native(2); // Medium Impact
            } else {
                kudroid_vibrate_native(3); // Heavy Impact
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