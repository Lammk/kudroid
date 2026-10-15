package android.os;

public class SystemVibrator extends Vibrator {
    public SystemVibrator() {}
    public boolean hasVibrator() { return true; }
    public boolean hasAmplitudeControl() { return false; }
    public void cancel() {}
}
