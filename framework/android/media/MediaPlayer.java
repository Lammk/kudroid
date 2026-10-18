package android.media;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.view.Surface;
import android.view.SurfaceHolder;
import java.io.FileDescriptor;
import java.io.IOException;
import java.util.HashMap;

public class MediaPlayer {
    public interface OnCompletionListener { void onCompletion(MediaPlayer mp); }
    public interface OnErrorListener { boolean onError(MediaPlayer mp, int what, int extra); }
    public interface OnPreparedListener { void onPrepared(MediaPlayer mp); }
    public interface OnVideoSizeChangedListener { void onVideoSizeChanged(MediaPlayer mp, int width, int height); }
    public interface OnSeekCompleteListener { void onSeekComplete(MediaPlayer mp); }
    public interface OnBufferingUpdateListener { void onBufferingUpdate(MediaPlayer mp, int percent); }

    public static final int MEDIA_ERROR_UNKNOWN = 1;
    public static final int MEDIA_ERROR_UNSUPPORTED = -1010;
    public static final int VIDEO_SCALING_MODE_SCALE_TO_FIT = 1;
    public static final int VIDEO_SCALING_MODE_SCALE_TO_FIT_WITH_CROPPING = 2;
    private static final int PREPARE = 1, START = 2, PAUSE = 3, STOP = 4, RELEASE = 5;
    private static final int POSITION = 6, DURATION = 7, WIDTH = 8, HEIGHT = 9;
    private static final int SEEK = 10, VOLUME = 11, LOOP = 12, SURFACE = 13, PLAYING = 14, STATUS = 15;
    private static final HashMap<Long, MediaPlayer> sPlayers = new HashMap<Long, MediaPlayer>();
    private long mHandle;
    private boolean mLooping;
    private OnCompletionListener mCompletionListener;
    private OnErrorListener mErrorListener;
    private OnPreparedListener mPreparedListener;
    private OnVideoSizeChangedListener mSizeListener;
    private OnSeekCompleteListener mSeekListener;
    private OnBufferingUpdateListener mBufferingListener;

    private static native long nativeCreate();
    private static native int nativeSource(long handle, String path, int fd, long offset, long length);
    private static native long nativeCommand(long handle, int command, double a, double b);

    public MediaPlayer() { allocate(); }
    private void allocate() {
        mHandle = nativeCreate();
        if (mHandle == 0) throw new IllegalStateException("Media playback is unavailable");
        synchronized (sPlayers) { sPlayers.put(Long.valueOf(mHandle), this); }
    }
    private long command(int op, double a, double b) {
        if (mHandle == 0) throw new IllegalStateException("MediaPlayer has been released");
        return nativeCommand(mHandle, op, a, b);
    }
    private void checkedCommand(int op, double a, double b) {
        if (command(op, a, b) < 0) throw new IllegalStateException("Invalid MediaPlayer state");
    }
    public static MediaPlayer create(Context context, int resid) {
        MediaPlayer player = null;
        AssetFileDescriptor asset = null;
        try {
            asset = context.getResources().openRawResourceFd(resid);
            if (asset == null) return null;
            player = new MediaPlayer();
            player.setDataSource(asset.getFileDescriptor(), asset.getStartOffset(), asset.getLength());
            player.prepare();
            return player;
        } catch (Exception error) {
            if (player != null) player.release();
            return null;
        } finally {
            if (asset != null) try { asset.close(); } catch (IOException ignored) {}
        }
    }
    public void setDataSource(String path) throws IOException {
        if (path == null) throw new IllegalArgumentException("null source");
        if (mHandle == 0) throw new IllegalStateException("MediaPlayer has been released");
        if (nativeSource(mHandle, path, -1, 0, -1) < 0) throw new IOException("Cannot open media source");
    }
    public void setDataSource(Context context, Uri uri) throws IOException {
        if (uri == null) throw new IllegalArgumentException("null source");
        setDataSource("file".equals(uri.getScheme()) ? uri.getPath() : uri.toString());
    }
    public void setDataSource(FileDescriptor fd) throws IOException { setDataSource(fd, 0, -1); }
    public void setDataSource(FileDescriptor fd, long offset, long length) throws IOException {
        if (fd == null || offset < 0 || length < -1) throw new IllegalArgumentException("Invalid media range");
        if (mHandle == 0) throw new IllegalStateException("MediaPlayer has been released");
        if (nativeSource(mHandle, null, fd.getInt$(), offset, length) < 0) throw new IOException("Cannot read media range");
    }
    public void prepare() throws IOException {
        checkedCommand(PREPARE, 0, 0);
        long deadline = android.os.SystemClock.uptimeMillis() + 30000;
        while (command(STATUS, 0, 0) == 0) {
            if (android.os.SystemClock.uptimeMillis() >= deadline) throw new IOException("Media preparation timed out");
            try { Thread.sleep(10); } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IOException("Media preparation interrupted");
            }
        }
        if (command(STATUS, 0, 0) < 0) throw new IOException("Unsupported or invalid media");
    }
    public void prepareAsync() { checkedCommand(PREPARE, 0, 0); }
    public void setDisplay(SurfaceHolder holder) { setSurface(holder == null ? null : holder.getSurface()); }
    public void setSurface(Surface surface) {
        if (surface != null && surface.mSurfaceTextureId != 0) {
            throw new UnsupportedOperationException("MediaPlayer texture output is unavailable");
        }
        checkedCommand(SURFACE, surface == null ? 0 : 1, 0);
    }
    public int getVideoWidth() { return (int) command(WIDTH, 0, 0); }
    public int getVideoHeight() { return (int) command(HEIGHT, 0, 0); }
    public void start() { checkedCommand(START, 0, 0); }
    public void stop() { checkedCommand(STOP, 0, 0); }
    public void pause() { checkedCommand(PAUSE, 0, 0); }
    public boolean isPlaying() { return command(PLAYING, 0, 0) > 0; }
    public void seekTo(int msec) { checkedCommand(SEEK, Math.max(0, msec), 0); }
    public int getCurrentPosition() { return (int) command(POSITION, 0, 0); }
    public int getDuration() { return (int) command(DURATION, 0, 0); }
    public void setVolume(float leftVolume, float rightVolume) { checkedCommand(VOLUME, leftVolume, rightVolume); }
    public void setLooping(boolean looping) { checkedCommand(LOOP, looping ? 1 : 0, 0); mLooping = looping; }
    public boolean isLooping() { return mLooping; }
    public void reset() { release(); mLooping = false; allocate(); }
    public void release() {
        if (mHandle == 0) return;
        synchronized (sPlayers) { sPlayers.remove(Long.valueOf(mHandle)); }
        nativeCommand(mHandle, RELEASE, 0, 0);
        mHandle = 0;
    }
    public void setAudioStreamType(int type) {}
    public void setScreenOnWhilePlaying(boolean screenOn) {}
    public void setVideoScalingMode(int mode) {}
    public void setOnCompletionListener(OnCompletionListener listener) { mCompletionListener = listener; }
    public void setOnErrorListener(OnErrorListener listener) { mErrorListener = listener; }
    public void setOnPreparedListener(OnPreparedListener listener) { mPreparedListener = listener; }
    public void setOnVideoSizeChangedListener(OnVideoSizeChangedListener listener) { mSizeListener = listener; }
    public void setOnSeekCompleteListener(OnSeekCompleteListener listener) { mSeekListener = listener; }
    public void setOnBufferingUpdateListener(OnBufferingUpdateListener listener) { mBufferingListener = listener; }

    // Called by the main Looper, never by the decoder thread.
    public static void notifyEvent(long handle, int event, int extra) {
        MediaPlayer player;
        synchronized (sPlayers) { player = sPlayers.get(Long.valueOf(handle)); }
        if (player == null) return;
        if (event == 1) {
            player.notifySize();
            if (player.mHandle != handle) return;
            if (player.mPreparedListener != null) player.mPreparedListener.onPrepared(player);
        } else if (event == 2) {
            if (player.mCompletionListener != null) player.mCompletionListener.onCompletion(player);
        } else if (event == 4) {
            if (player.mSeekListener != null) player.mSeekListener.onSeekComplete(player);
        } else if (event == 5) {
            player.notifySize();
        } else if (event == 100) {
            boolean handled = player.mErrorListener != null && player.mErrorListener.onError(player, MEDIA_ERROR_UNKNOWN, extra);
            if (!handled && player.mHandle == handle && player.mCompletionListener != null) player.mCompletionListener.onCompletion(player);
        }
    }
    private void notifySize() {
        int width = getVideoWidth(), height = getVideoHeight();
        if (mSizeListener != null && width > 0 && height > 0) mSizeListener.onVideoSizeChanged(this, width, height);
    }
}
