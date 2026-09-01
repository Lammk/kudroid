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
    public void unregisterAudioDeviceCallback(AudioDeviceCallback callback) {}
    public int getMode() { return MODE_NORMAL; }
    public void setMode(int mode) {}
}
