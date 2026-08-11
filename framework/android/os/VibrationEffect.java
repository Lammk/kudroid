package android.os;

/**
 * Stub android.os.VibrationEffect.
 *
 * Describes a vibration effect. For KuDroid's minimal framework, this is a
 * stub.
 */
public abstract class VibrationEffect {
    /** Default amplitude. */
    public static final int DEFAULT_AMPLITUDE = -1;

    /**
     * Create a one-shot vibration effect.
     */
    public static VibrationEffect createOneShot(long milliseconds, int amplitude) {
        return new OneShot(milliseconds, amplitude);
    }

    /**
     * Create a waveform vibration effect.
     */
    public static VibrationEffect createWaveform(long[] timings, int repeat) {
        return new Waveform(timings, repeat);
    }

    /**
     * A one-shot vibration effect.
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
     * A waveform vibration effect.
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