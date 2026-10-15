package android.os;

public abstract class Vibrator {
    protected Vibrator() {}
    public abstract boolean hasVibrator();
    public abstract boolean hasAmplitudeControl();
    public void vibrate(long milliseconds) {}
    public void vibrate(long[] pattern, int repeat) {}
    public void vibrate(VibrationEffect vibe) {}
    public abstract void cancel();
}
