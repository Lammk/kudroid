package android.graphics;

/**
 * Minimal android.graphics.SurfaceTexture.
 *
 * Unity's VideoPlayer renders decoded frames into one of these and is told
 * about new frames through OnFrameAvailableListener. KuDroid has no media
 * decoder, so no frame ever arrives: the listener is stored and simply never
 * fired, updateTexImage is a no-op, and the transform stays identity. What
 * matters is that the class and its listener interface EXIST — without them
 * the video path dies as NoClassDefFoundError during scene setup instead of
 * failing cleanly later.
 */
public class SurfaceTexture {

    /** Fired when a decoded frame lands; never fires here (see above). */
    public interface OnFrameAvailableListener {
        void onFrameAvailable(SurfaceTexture surfaceTexture);
    }

    private int mTexName;
    private OnFrameAvailableListener mListener;
    private boolean mReleased;

    public SurfaceTexture(int texName) {
        mTexName = texName;
    }

    public SurfaceTexture(int texName, boolean singleBufferMode) {
        mTexName = texName;
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener) {
        mListener = listener;
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener,
                                            android.os.Handler handler) {
        mListener = listener;
    }

    /** No frame ever arrives, so this only validates state. */
    public void updateTexImage() {
    }

    /** Identity matrix: no rotation/scale is ever applied to a missing frame. */
    public void getTransformMatrix(float[] mtx) {
        if (mtx == null || mtx.length < 16) return;
        for (int i = 0; i < 16; i++) {
            mtx[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
    }

    public long getTimestamp() {
        return 0;
    }

    public void setDefaultBufferSize(int width, int height) {
    }

    public void detachFromGLContext() {
    }

    public void attachToGLContext(int texName) {
        mTexName = texName;
    }

    public void release() {
        mReleased = true;
        mListener = null;
    }

    public boolean isReleased() {
        return mReleased;
    }
}
