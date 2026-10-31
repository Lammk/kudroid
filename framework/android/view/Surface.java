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
    /** Id of the SurfaceTexture this Surface feeds, 0 for a screen surface. Read natively. */
    public int mSurfaceTextureId = 0;
    private boolean mValid = true;
    private int mWidth;
    private int mHeight;
    private boolean mOwnsNativeObject;
    private static native long nativeSetFrame(long handle, int x, int y, int width, int height);
    private static native void nativeRelease(long handle);

    public Surface() {
        this.mValid = true;
    }

    public Surface(long nativeObject) {
        this.mNativeObject = nativeObject;
        this.mValid = true;
    }

    /** A Surface whose buffers go to {@code surfaceTexture}. */
    public Surface(android.graphics.SurfaceTexture surfaceTexture) {
        if (surfaceTexture == null) {
            throw new IllegalArgumentException("surfaceTexture must not be null");
        }
        this.mSurfaceTextureId = surfaceTexture.getKuDroidId();
        this.mValid = true;
    }

    /** Checks that the surface is still valid for drawing. */
    public boolean isValid() {
        return mValid;
    }

    public void release() {
        clearSurface();
        mValid = false;
        mWidth = 0;
        mHeight = 0;
        mNativeObject = 0;
    }

    void setSurfaceSize(int width, int height) {
        setSurfaceFrame(0, 0, width, height);
    }

    void setSurfaceFrame(int x, int y, int width, int height) {
        mWidth = Math.max(0, width);
        mHeight = Math.max(0, height);
        mValid = true;
        if (mSurfaceTextureId == 0 && mWidth > 0 && mHeight > 0) {
            boolean create = mNativeObject == 0;
            mNativeObject = nativeSetFrame(mNativeObject, x, y, mWidth, mHeight);
            if (create) mOwnsNativeObject = mNativeObject != 0;
        }
    }

    void clearSurface() {
        long handle = mNativeObject;
        mNativeObject = 0;
        if (handle != 0 && mOwnsNativeObject) nativeRelease(handle);
        mOwnsNativeObject = false;
        mValid = false;
        mWidth = 0;
        mHeight = 0;
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
        return mWidth;
    }

    public int getHeight() {
        return mHeight;
    }
}
