package android.os;

public abstract class VibrationEffect implements Parcelable {
    public static final int DEFAULT_AMPLITUDE = -1;
    public static VibrationEffect createOneShot(long milliseconds, int amplitude) { return null; }
    public static VibrationEffect createWaveform(long[] timings, int repeat) { return null; }
    public static VibrationEffect createWaveform(long[] timings, int[] amplitudes, int repeat) { return null; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
