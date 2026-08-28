package android.media;

import android.content.Context;
import java.io.FileDescriptor;
import java.io.IOException;

public class MediaPlayer {
    public interface OnCompletionListener { void onCompletion(MediaPlayer mp); }
    public interface OnErrorListener { boolean onError(MediaPlayer mp, int what, int extra); }
    public interface OnPreparedListener { void onPrepared(MediaPlayer mp); }

    public MediaPlayer() {}
    public static MediaPlayer create(Context context, int resid) { return new MediaPlayer(); }
    public void setDataSource(String path) throws IOException {}
    public void setDataSource(FileDescriptor fd) throws IOException {}
    public void setDataSource(FileDescriptor fd, long offset, long length) throws IOException {}
    public void prepare() throws IOException {}
    public void prepareAsync() {}
    public void start() {}
    public void stop() {}
    public void pause() {}
    public boolean isPlaying() { return false; }
    public void seekTo(int msec) {}
    public int getCurrentPosition() { return 0; }
    public int getDuration() { return 0; }
    public void setVolume(float leftVolume, float rightVolume) {}
    public void setLooping(boolean looping) {}
    public boolean isLooping() { return false; }
    public void reset() {}
    public void release() {}
    public void setOnCompletionListener(OnCompletionListener listener) {}
    public void setOnErrorListener(OnErrorListener listener) {}
    public void setOnPreparedListener(OnPreparedListener listener) {}
}
