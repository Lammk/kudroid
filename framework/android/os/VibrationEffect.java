package android.os;

/**
 * simulate android.os.vibrationeffect.
 *
 * describes a vibrating effect. for kudroid minimal framework, here is one
 * simulation.
 */
public abstract class VibrationEffect {
    /** default amplitude. */
    public static final int DEFAULT_AMPLITUDE = -1;

    /**
     * create a one-time vibration effect.
     */
    public static VibrationEffect createOneShot(long milliseconds, int amplitude) {
        return new OneShot(milliseconds, amplitude);
    }

    /**
     * creates a wave-like vibration effect.
     */
    public static VibrationEffect createWaveform(long[] timings, int repeat) {
        return new Waveform(timings, repeat);
    }

    /**
     * a one-time vibration effect.
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
     * a vibrating wave effect.
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