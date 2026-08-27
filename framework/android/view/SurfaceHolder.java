package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Minimum stub of android.view.SurfaceHolder for KuDroid.
 *
 * Classic Android games (MCPE etc.) declared
 *   class MainActivity extends SurfaceView implements SurfaceHolder.Callback2
 * so the JVM is required to resolve this interface when loading the class — missing it
 * causes ClassNotFoundException even before onCreate runs.
 */
public interface SurfaceHolder {
    /** Surface buffer type. */
    public static final int SURFACE_TYPE_NORMAL = 0;
    public static final int SURFACE_TYPE_HARDWARE = 1;
    public static final int SURFACE_TYPE_GPU = 2;
    public static final int SURFACE_TYPE_PUSH_BUFFERS = 3;

    /** Basic callback: create/change/destroy surface. */
    public interface Callback {
        void surfaceCreated(SurfaceHolder holder);
        void surfaceChanged(SurfaceHolder holder, int format, int width, int height);
        void surfaceDestroyed(SurfaceHolder holder);
    }

    /**
     * Callback2 extends Callback with surfaceRedrawNeeded — Activity used
     * surface as the main window (window takes surface) needs to implement this.
     */
    public interface Callback2 extends Callback {
        void surfaceRedrawNeeded(SurfaceHolder holder);
    }

    /** Add callback to receive surface events. */
    void addCallback(Callback callback);

    void removeCallback(Callback callback);

    /** Returns this Surface backing view. */
    Surface getSurface();

    Rect getSurfaceFrame();

    boolean isCreating();

    @Deprecated
    void setType(int type);

    void setFixedSize(int width, int height);

    void setSizeFromLayout();

    void setFormat(int format);

    void setKeepScreenOn(boolean screenOn);

    Canvas lockCanvas();

    Canvas lockCanvas(Rect dirty);

    void unlockCanvasAndPost(Canvas canvas);

    @Deprecated
    Canvas lockCanvasAndroidOnly(Rect dirty);
}
