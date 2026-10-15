package android.media;

import android.content.Context;
import java.io.FileDescriptor;
import java.io.IOException;

public class MediaPlayer {
    public interface OnCompletionListener { void onCompletion(MediaPlayer mp); }
    public interface OnErrorListener { boolean onError(MediaPlayer mp, int what, int extra); }
    public interface OnPreparedListener { void onPrepared(MediaPlayer mp); }

    private OnCompletionListener mCompletionListener;
    private OnErrorListener mErrorListener;
    private OnPreparedListener mPreparedListener;
    private boolean mPlaying = false;

    public MediaPlayer() {}
    public static MediaPlayer create(Context context, int resid) { return new MediaPlayer(); }
    public void setDataSource(String path) throws IOException {}
    public void setDataSource(FileDescriptor fd) throws IOException {}
    public void setDataSource(FileDescriptor fd, long offset, long length) throws IOException {}
    public void prepare() throws IOException {}
    public void prepareAsync() {
        final OnPreparedListener listener = mPreparedListener;
        if (listener != null) {
            new Thread(new Runnable() {
                public void run() {
                    listener.onPrepared(MediaPlayer.this);
                }
            }).start();
        }
    }
    public void start() { mPlaying = true; }
    public void stop() { mPlaying = false; }
    public void pause() { mPlaying = false; }
    public boolean isPlaying() { return mPlaying; }
    public void seekTo(int msec) {}
    public int getCurrentPosition() { return 0; }
    public int getDuration() { return 0; }
    public void setVolume(float leftVolume, float rightVolume) {}
    public void setLooping(boolean looping) {}
    public boolean isLooping() { return false; }
    public void reset() {}
    public void release() {}
    public void setOnCompletionListener(OnCompletionListener listener) { mCompletionListener = listener; }
    public void setOnErrorListener(OnErrorListener listener) { mErrorListener = listener; }
    public void setOnPreparedListener(OnPreparedListener listener) { mPreparedListener = listener; }
}
