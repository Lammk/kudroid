package android.view;

import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Handler;

public final class PixelCopy {
    public static final int SUCCESS = 0;
    public static final int ERROR_UNKNOWN = 1;
    public static final int ERROR_TIMEOUT = 2;
    public static final int ERROR_SOURCE_NO_DATA = 3;
    public static final int ERROR_SOURCE_INVALID = 4;
    public static final int ERROR_DESTINATION_INVALID = 5;

    public interface OnPixelCopyFinishedListener {
        void onPixelCopyFinished(int copyResult);
    }

    private PixelCopy() {}

    private static void notifyListener(final OnPixelCopyFinishedListener listener, Handler handler) {
        if (listener == null) return;
        if (handler != null) {
            handler.post(new Runnable() {
                @Override
                public void run() {
                    listener.onPixelCopyFinished(SUCCESS);
                }
            });
        } else {
            listener.onPixelCopyFinished(SUCCESS);
        }
    }

    public static void request(SurfaceView source, Bitmap dest, OnPixelCopyFinishedListener listener, Handler listenerThread) {
        notifyListener(listener, listenerThread);
    }

    public static void request(SurfaceView source, Rect srcRect, Bitmap dest, OnPixelCopyFinishedListener listener, Handler listenerThread) {
        notifyListener(listener, listenerThread);
    }

    public static void request(Surface source, Bitmap dest, OnPixelCopyFinishedListener listener, Handler listenerThread) {
        notifyListener(listener, listenerThread);
    }

    public static void request(Surface source, Rect srcRect, Bitmap dest, OnPixelCopyFinishedListener listener, Handler listenerThread) {
        notifyListener(listener, listenerThread);
    }

    public static void request(Window source, Bitmap dest, OnPixelCopyFinishedListener listener, Handler listenerThread) {
        notifyListener(listener, listenerThread);
    }

    public static void request(Window source, Rect srcRect, Bitmap dest, OnPixelCopyFinishedListener listener, Handler listenerThread) {
        notifyListener(listener, listenerThread);
    }
}
