package android.media;

import android.content.Context;

public class SoundPool {
    public SoundPool(int maxStreams, int streamType, int srcQuality) {}
    public int load(String path, int priority) { return 1; }
    public int load(Context context, int resId, int priority) { return 1; }
    public int play(int soundID, float leftVolume, float rightVolume, int priority, int loop, float rate) { return 1; }
    public void pause(int streamID) {}
    public void resume(int streamID) {}
    public void stop(int streamID) {}
    public void setVolume(int streamID, float leftVolume, float rightVolume) {}
    public void setRate(int streamID, float rate) {}
    public void setLoop(int streamID, int loop) {}
    public boolean unload(int soundID) { return true; }
    public void release() {}
}
