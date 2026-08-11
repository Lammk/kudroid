package android.media;

/**
 * mô phỏng android.media.audiomanager.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về mặc định để các ứng dụng không
 * gặp sự cố khi chúng truy vấn trạng thái âm thanh.
 */
public class AudioManager {
    /** loại luồng: âm nhạc. */
    public static final int STREAM_MUSIC = 3;
    /** loại luồng: hệ thống. */
    public static final int STREAM_SYSTEM = 1;
    /** loại luồng: cuộc gọi thoại. */
    public static final int STREAM_VOICE_CALL = 0;
    /** loại luồng: báo thức. */
    public static final int STREAM_ALARM = 4;
    /** loại luồng: thông báo. */
    public static final int STREAM_NOTIFICATION = 5;

    /** chế độ âm thanh: bình thường. */
    public static final int MODE_NORMAL = 0;
    /** chế độ âm thanh: nhạc chuông. */
    public static final int MODE_RINGTONE = 1;
    /** chế độ âm thanh: trong cuộc gọi. */
    public static final int MODE_IN_CALL = 2;

    /** chế độ chuông: bình thường. */
    public static final int RINGER_MODE_NORMAL = 2;
    /** chế độ chuông: im lặng. */
    public static final int RINGER_MODE_SILENT = 0;
    /** chế độ chuông: rung. */
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
     * trình nghe cho các thay đổi tiêu điểm âm thanh.
     */
    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }
}