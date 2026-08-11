package android.os;

/**
 * Stub android.os.Vibrator.
 *
 * Non-critical for app startup/rendering. Vibration is a no-op on iOS.
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