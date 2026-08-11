package android.os;

/**
 * mô phỏng android.os.vibrationeffect.
 *
 * mô tả một hiệu ứng rung. đối với khuôn khổ tối thiểu của kudroid, đây là một
 * mô phỏng.
 */
public abstract class VibrationEffect {
    /** biên độ mặc định. */
    public static final int DEFAULT_AMPLITUDE = -1;

    /**
     * tạo một hiệu ứng rung một lần.
     */
    public static VibrationEffect createOneShot(long milliseconds, int amplitude) {
        return new OneShot(milliseconds, amplitude);
    }

    /**
     * tạo một hiệu ứng rung dạng sóng.
     */
    public static VibrationEffect createWaveform(long[] timings, int repeat) {
        return new Waveform(timings, repeat);
    }

    /**
     * một hiệu ứng rung một lần.
     */
    public static class OneShot extends VibrationEffect {
        public final long milliseconds;
        public final int amplitude;

        public OneShot(long milliseconds, int amplitude) {
            this.milliseconds = milliseconds;
            this.amplitude = amplitude;
        }
    }

    /**
     * một hiệu ứng rung dạng sóng.
     */
    public static class Waveform extends VibrationEffect {
        public final long[] timings;
        public final int repeat;

        public Waveform(long[] timings, int repeat) {
            this.timings = timings;
            this.repeat = repeat;
        }
    }
}