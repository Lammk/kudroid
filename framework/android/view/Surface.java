package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Minimum stub of android.view.Surface for KuDroid.
 *
 * Only provides APIs commonly called by canvas-based games. Realistic drawing comes through
 * Metal Canvas native pipeline (android/graphics/Canvas bridged to C++),
 * so the methods here just need to not crash and return a valid object.
 */
public class Surface {
    public static final int ROTATION_0 = 0;
    public static final int ROTATION_90 = 1;
    public static final int ROTATION_180 = 2;
    public static final int ROTATION_270 = 3;

    private Object mNativeSurface = null;
    private boolean mValid = true;

    public Surface() {
    }

    /** Checks that the surface is still valid for drawing. */
    public boolean isValid() {
        return mValid;
    }

    public void release() {
        mValid = false;
    }

    /** Locks the software canvas for 2D drawing. */
    public Canvas lockCanvas() {
        return lockCanvas(null);
    }

    public Canvas lockCanvas(Rect dirty) {
        // KuDroid's canvas stub bridges down to the C++ Metal pipeline.
        return new Canvas();
    }

    /** Canvas hardware-accelerated — on KuDroid like regular lockCanvas. */
    public Canvas lockHardwareCanvas() {
        return new Canvas();
    }

    /** Register the drawn canvas to the compositor. */
    public void unlockCanvasAndPost(Canvas canvas) {
        // no-op: native pipeline presents itself via CAMetalLayer.
    }

    @Deprecated
    public void unlockCanvas(Canvas canvas) {
        // legacy API, kept for legacy bytecode compatibility.
    }

    /** Current size of the surface (width/height overwritten by native). */
    public int getWidth() {
        return 0;
    }

    public int getHeight() {
        return 0;
    }
}
