package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Standard stub of android.view.Surface for KuDroid.
 *
 * mNativeObject stores the pointer to the underlying KuDroidNativeWindow (ANativeWindow)
 * which links to the CAMetalLayer for hardware-accelerated 3D rendering.
 */
public class Surface {
    public static final int ROTATION_0 = 0;
    public static final int ROTATION_90 = 1;
    public static final int ROTATION_180 = 2;
    public static final int ROTATION_3 = 3;

    public long mNativeObject = 0;
    private boolean mValid = true;

    public Surface() {
    }

    public Surface(long nativeObject) {
        this.mNativeObject = nativeObject;
    }

    /** Checks that the surface is still valid for drawing. */
    public boolean isValid() {
        return mValid;
    }

    public void release() {
        mValid = false;
        mNativeObject = 0;
    }

    /** Locks the software canvas for 2D drawing. */
    public Canvas lockCanvas() {
        return lockCanvas(null);
    }

    public Canvas lockCanvas(Rect dirty) {
        return new Canvas();
    }

    public Canvas lockHardwareCanvas() {
        return new Canvas();
    }

    public void unlockCanvasAndPost(Canvas canvas) {
    }

    @Deprecated
    public void unlockCanvas(Canvas canvas) {
    }

    public int getWidth() {
        return 0;
    }

    public int getHeight() {
        return 0;
    }
}
