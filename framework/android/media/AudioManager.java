package android.media;

public class AudioManager {
    public static final int STREAM_VOICE_CALL = 0;
    public static final int STREAM_SYSTEM = 1;
    public static final int STREAM_RING = 2;
    public static final int STREAM_MUSIC = 3;
    public static final int STREAM_ALARM = 4;
    public static final int STREAM_NOTIFICATION = 5;

    public static final int MODE_NORMAL = 0;
    public static final int MODE_RINGTONE = 1;
    public static final int MODE_IN_CALL = 2;
    public static final int MODE_IN_COMMUNICATION = 3;

    public AudioManager() {}
    public int getStreamVolume(int streamType) { return 10; }
    public int getStreamMaxVolume(int streamType) { return 15; }
    public int getStreamMinVolume(int streamType) { return 0; }
    public boolean isMusicActive() { return false; }
    public boolean isSpeakerphoneOn() { return true; }
    public void setSpeakerphoneOn(boolean on) {}
    public boolean isWiredHeadsetOn() { return false; }
    public boolean isBluetoothScoOn() { return false; }
    public void setBluetoothScoOn(boolean on) {}
    public AudioDeviceInfo[] getDevices(int flags) {
        return new AudioDeviceInfo[] { new AudioDeviceInfo(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, 1) };
    }
    public void registerAudioDeviceCallback(AudioDeviceCallback callback, android.os.Handler handler) {}
    public static final int AUDIOFOCUS_GAIN = 1;
    public static final int AUDIOFOCUS_GAIN_TRANSIENT = 2;
    public static final int AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK = 3;
    public static final int AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE = 4;
    public static final int AUDIOFOCUS_LOSS = -1;
    public static final int AUDIOFOCUS_LOSS_TRANSIENT = -2;
    public static final int AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK = -3;
    public static final int AUDIOFOCUS_REQUEST_FAILED = 0;
    public static final int AUDIOFOCUS_REQUEST_GRANTED = 1;
    public static final int AUDIOFOCUS_REQUEST_DELAYED = 2;

    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }

    public int requestAudioFocus(OnAudioFocusChangeListener l, int streamType, int durationHint) {
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    public int abandonAudioFocus(OnAudioFocusChangeListener l) {
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    public int getMode() { return MODE_NORMAL; }
    public void setMode(int mode) {}

    // ── getProperty ──────────────────────────────────────────────────────────
    //
    // How an app discovers the device's audio geometry before opening a stream. Unity reads
    // both keys below during startup to size its mixer, and FMOD does the same.
    //
    // The values are read from the SAME source AudioTrack uses (AudioShim, via
    // AudioTrack.getNativeOutputSampleRate and getMinBufferSize) rather than being constants
    // here. Two different answers for one device is worse than either answer alone: the app
    // sizes its buffers from these strings and then writes into a stream configured from the
    // other path, so every buffer is the wrong length for the life of the process.
    public static final String PROPERTY_OUTPUT_SAMPLE_RATE = "android.media.property.OUTPUT_SAMPLE_RATE";
    public static final String PROPERTY_OUTPUT_FRAMES_PER_BUFFER =
            "android.media.property.OUTPUT_FRAMES_PER_BUFFER";
    public static final String PROPERTY_SUPPORT_MIC_NEAR_ULTRASOUND =
            "android.media.property.SUPPORT_MIC_NEAR_ULTRASOUND";
    public static final String PROPERTY_SUPPORT_SPEAKER_NEAR_ULTRASOUND =
            "android.media.property.SUPPORT_SPEAKER_NEAR_ULTRASOUND";
    public static final String PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED =
            "android.media.property.SUPPORT_AUDIO_SOURCE_UNPROCESSED";

    /**
     * Returns null for an unknown key, which is what the platform does and what callers
     * handle — a made-up value would be taken as fact.
     */
    public String getProperty(String key) {
        if (key == null) {
            return null;
        }
        if (key.equals(PROPERTY_OUTPUT_SAMPLE_RATE)) {
            return Integer.toString(
                    android.media.AudioTrack.getNativeOutputSampleRate(STREAM_MUSIC));
        }
        if (key.equals(PROPERTY_OUTPUT_FRAMES_PER_BUFFER)) {
            // Frames, not bytes — that is what the key means, and passing bytes here would
            // make an app allocate four times too much and report a latency it never has.
            final int rate = android.media.AudioTrack.getNativeOutputSampleRate(STREAM_MUSIC);
            final int bytes = android.media.AudioTrack.getMinBufferSize(
                    rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
            final int frames = bytes > 0 ? bytes / 4 : 0;  // 2 channels * 2 bytes
            return Integer.toString(frames > 0 ? frames : 256);
        }
        if (key.equals(PROPERTY_SUPPORT_MIC_NEAR_ULTRASOUND)
                || key.equals(PROPERTY_SUPPORT_SPEAKER_NEAR_ULTRASOUND)
                || key.equals(PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED)) {
            return "false";
        }
        return null;
    }

    // ── getDevices flags ─────────────────────────────────────────────────────
    //
    // getDevices() already exists above and returns the built-in speaker; these are the
    // masks that say which direction to report. An app passes GET_DEVICES_OUTPUTS to
    // enumerate outputs, so without the constant the call site cannot be written at all.
    public static final int GET_DEVICES_OUTPUTS = 0x2;
    public static final int GET_DEVICES_INPUTS = 0x1;
    public static final int GET_DEVICES_ALL = GET_DEVICES_OUTPUTS | GET_DEVICES_INPUTS;

    public void unregisterAudioDeviceCallback(AudioDeviceCallback callback) {}
}
