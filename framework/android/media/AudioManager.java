package android.media;

/**
 * emulate android.media.audiomanager.
 *
 * is not important for application startup/rendering. Returns default to no applications
 * crashes when they query audio status.
 */
public class AudioManager {
    /** stream type: music. */
    public static final int STREAM_MUSIC = 3;
    /** thread type: system. */
    public static final int STREAM_SYSTEM = 1;
    /** stream type: voice call. */
    public static final int STREAM_VOICE_CALL = 0;
    /** stream type: alarm. */
    public static final int STREAM_ALARM = 4;
    /** stream type: message. */
    public static final int STREAM_NOTIFICATION = 5;

    /** sound mode: normal. */
    public static final int MODE_NORMAL = 0;
    /** sound mode: ringtone. */
    public static final int MODE_RINGTONE = 1;
    /** audio mode: in call. */
    public static final int MODE_IN_CALL = 2;

    /** ring mode: normal. */
    public static final int RINGER_MODE_NORMAL = 2;
    /** ring mode: silent. */
    public static final int RINGER_MODE_SILENT = 0;
    /** ring mode: vibrate. */
    public static final int RINGER_MODE_VIBRATE = 1;

    public AudioManager() {
    }

    public int getStreamVolume(int streamType) {
        return 0;
    }

    public void setStreamVolume(int streamType, int index, int flags) {
    }

    public int getStreamMaxVolume(int streamType) {
        return 15;
    }

    public int getMode() {
        return MODE_NORMAL;
    }

    public void setMode(int mode) {
    }

    public int getRingerMode() {
        return RINGER_MODE_NORMAL;
    }

    public void setRingerMode(int ringerMode) {
    }

    public boolean isMusicActive() {
        return false;
    }

    public void setSpeakerphoneOn(boolean on) {
    }

    public boolean isSpeakerphoneOn() {
        return false;
    }

    public void setMicrophoneMute(boolean on) {
    }

    public boolean isMicrophoneMute() {
        return false;
    }

    public void setBluetoothScoOn(boolean on) {
    }

    public boolean isBluetoothScoOn() {
        return false;
    }

    public void startBluetoothSco() {
    }

    public void stopBluetoothSco() {
    }

    public void requestAudioFocus(android.media.AudioManager.OnAudioFocusChangeListener l,
                                  int streamType, int durationHint) {
    }

    public void abandonAudioFocus(android.media.AudioManager.OnAudioFocusChangeListener l) {
    }

    /**
     * listener for audio focus changes.
     */
    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }
}