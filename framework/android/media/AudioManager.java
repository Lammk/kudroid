package android.media;

/**
 * Stub android.media.AudioManager.
 *
 * Non-critical for app startup/rendering. Returns defaults so apps don't
 * crash when they query audio state.
 */
public class AudioManager {
    /** Stream type: music. */
    public static final int STREAM_MUSIC = 3;
    /** Stream type: system. */
    public static final int STREAM_SYSTEM = 1;
    /** Stream type: voice call. */
    public static final int STREAM_VOICE_CALL = 0;
    /** Stream type: alarm. */
    public static final int STREAM_ALARM = 4;
    /** Stream type: notification. */
    public static final int STREAM_NOTIFICATION = 5;

    /** Audio mode: normal. */
    public static final int MODE_NORMAL = 0;
    /** Audio mode: ringtone. */
    public static final int MODE_RINGTONE = 1;
    /** Audio mode: in call. */
    public static final int MODE_IN_CALL = 2;

    /** Ringer mode: normal. */
    public static final int RINGER_MODE_NORMAL = 2;
    /** Ringer mode: silent. */
    public static final int RINGER_MODE_SILENT = 0;
    /** Ringer mode: vibrate. */
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
     * Listener for audio focus changes.
     */
    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }
}