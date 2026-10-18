package android.graphics;

/** Receives decoded frames and latches them on the current GL context. */
public class SurfaceTexture {

    /** Fired when a decoded frame becomes available. */
    public interface OnFrameAvailableListener {
        void onFrameAvailable(SurfaceTexture surfaceTexture);
    }

    private static final java.util.HashMap<Integer, SurfaceTexture> sLive =
            new java.util.HashMap<Integer, SurfaceTexture>();
    private static int sNextId = 1;

    private int mTexName;
    private final int mId;
    private volatile OnFrameAvailableListener mListener;
    private volatile android.os.Handler mHandler;
    private volatile long mTimestampNs;
    private boolean mReleased;
    private boolean mAttached = true;
    private native static void nativeCreate(int id, int texture);
    private native static void nativeRelease(int id);
    private native static void nativeAttach(int id, int texture);
    private native static void nativeDetach(int id);
    private native static long nativeUpdate(int id);

    public SurfaceTexture(int texName) {
        mTexName = texName;
        mId = register();
        nativeCreate(mId, texName);
    }

    public SurfaceTexture(int texName, boolean singleBufferMode) {
        mTexName = texName;
        mId = register();
        nativeCreate(mId, texName);
    }

    private int register() {
        synchronized (sLive) {
            int id = sNextId++;
            sLive.put(Integer.valueOf(id), this);
            return id;
        }
    }

    /** Identity the native codec uses to announce frames to this texture. */
    public int getKuDroidId() {
        return mId;
    }

    /** Called by the native codec when it renders a frame into a Surface on this texture. */
    public static void notifyFrameAvailable(int id, long timestampNs) {
        SurfaceTexture texture;
        synchronized (sLive) {
            texture = sLive.get(Integer.valueOf(id));
        }
        if (texture == null) return;
        final OnFrameAvailableListener listener = texture.mListener;
        if (listener != null) {
            final SurfaceTexture self = texture;
            android.os.Handler handler = texture.mHandler;
            if (handler != null) {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        listener.onFrameAvailable(self);
                    }
                });
            } else {
                listener.onFrameAvailable(self);
            }
        }
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener) {
        mListener = listener;
        mHandler = null;
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener,
                                            android.os.Handler handler) {
        mListener = listener;
        mHandler = handler;
    }

    public void updateTexImage() {
        if (mReleased || !mAttached) throw new IllegalStateException("SurfaceTexture is not attached");
        long timestamp = nativeUpdate(mId);
        if (timestamp < 0) throw new IllegalStateException("No current GL context");
        mTimestampNs = timestamp;
    }

    public void getTransformMatrix(float[] mtx) {
        if (mtx == null || mtx.length < 16) return;
        for (int i = 0; i < 16; i++) {
            mtx[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
        mtx[5] = -1.0f;
        mtx[13] = 1.0f;
    }

    public long getTimestamp() {
        return mTimestampNs;
    }

    public void setDefaultBufferSize(int width, int height) {
    }

    public void detachFromGLContext() {
        if (mReleased || !mAttached) throw new IllegalStateException("SurfaceTexture is not attached");
        nativeDetach(mId);
        mAttached = false;
    }

    public void attachToGLContext(int texName) {
        if (mReleased || mAttached) throw new IllegalStateException("SurfaceTexture is already attached");
        nativeAttach(mId, texName);
        mTexName = texName;
        mAttached = true;
    }

    public void release() {
        if (mReleased) return;
        mReleased = true;
        nativeRelease(mId);
        mListener = null;
        synchronized (sLive) {
            sLive.remove(Integer.valueOf(mId));
        }
    }

    public boolean isReleased() {
        return mReleased;
    }
}
