package android.opengl;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL;
import javax.microedition.khronos.opengles.GL10;

public class GLSurfaceView extends SurfaceView implements SurfaceHolder.Callback2 {
    private static final String TAG = "GLSurfaceView";

    public static final int RENDERMODE_WHEN_DIRTY = 0;
    public static final int RENDERMODE_CONTINUOUSLY = 1;

    public static final int DEBUG_CHECK_GL_ERROR = 1;
    public static final int DEBUG_LOG_GL_CALLS = 2;

    public interface Renderer {
        void onSurfaceCreated(GL10 gl, EGLConfig config);
        void onSurfaceChanged(GL10 gl, int width, int height);
        void onDrawFrame(GL10 gl);
    }

    public interface EGLConfigChooser {
        EGLConfig chooseConfig(EGL10 egl, EGLDisplay display);
    }

    public interface EGLContextFactory {
        EGLContext createContext(EGL10 egl, EGLDisplay display, EGLConfig eglConfig);
        void destroyContext(EGL10 egl, EGLDisplay display, EGLContext context);
    }

    public interface EGLWindowSurfaceFactory {
        EGLSurface createWindowSurface(EGL10 egl, EGLDisplay display, EGLConfig config, Object nativeWindow);
        void destroySurface(EGL10 egl, EGLDisplay display, EGLSurface surface);
    }

    public interface GLWrapper {
        GL wrap(GL gl);
    }

    private GLThread mGLThread;
    private Renderer mRenderer;
    private int mDebugFlags;
    private GLWrapper mGLWrapper;
    private boolean mPreserveEGLContextOnPause;
    private int mEGLContextClientVersion = 2;
    private int mRedSize = 8;
    private int mGreenSize = 8;
    private int mBlueSize = 8;
    private int mAlphaSize = 8;
    private int mDepthSize = 16;
    private int mStencilSize = 8;
    private EGLConfigChooser mEGLConfigChooser;
    private EGLContextFactory mEGLContextFactory;
    private EGLWindowSurfaceFactory mEGLWindowSurfaceFactory;

    public GLSurfaceView(Context context) {
        super(context);
        init();
    }

    public GLSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public GLSurfaceView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    public GLSurfaceView(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        init();
    }

    private void init() {
        getHolder().addCallback(this);
    }

    public void setGLWrapper(GLWrapper glWrapper) {
        mGLWrapper = glWrapper;
    }

    public void setDebugFlags(int debugFlags) {
        mDebugFlags = debugFlags;
    }

    public int getDebugFlags() {
        return mDebugFlags;
    }

    public void setPreserveEGLContextOnPause(boolean preserveOnPause) {
        mPreserveEGLContextOnPause = preserveOnPause;
    }

    public boolean getPreserveEGLContextOnPause() {
        return mPreserveEGLContextOnPause;
    }

    public void setEGLContextClientVersion(int version) {
        mEGLContextClientVersion = version;
    }

    public void setEGLConfigChooser(EGLConfigChooser configChooser) {
        mEGLConfigChooser = configChooser;
    }

    public void setEGLConfigChooser(boolean needDepth) {
        setEGLConfigChooser(8, 8, 8, 8, needDepth ? 16 : 0, 0);
    }

    public void setEGLConfigChooser(int redSize, int greenSize, int blueSize, int alphaSize,
                                    int depthSize, int stencilSize) {
        mRedSize = redSize;
        mGreenSize = greenSize;
        mBlueSize = blueSize;
        mAlphaSize = alphaSize;
        mDepthSize = depthSize;
        mStencilSize = stencilSize;
    }

    public void setEGLContextFactory(EGLContextFactory factory) {
        mEGLContextFactory = factory;
    }

    public void setEGLWindowSurfaceFactory(EGLWindowSurfaceFactory factory) {
        mEGLWindowSurfaceFactory = factory;
    }

    public void setRenderer(Renderer renderer) {
        if (mRenderer != null) {
            throw new IllegalStateException("setRenderer has already been called for this instance.");
        }
        mRenderer = renderer;
        mGLThread = new GLThread(this, mRenderer, mEGLContextClientVersion, mDepthSize, mStencilSize);
        mGLThread.start();
    }

    public void setRenderMode(int renderMode) {
        if (mGLThread != null) {
            mGLThread.setRenderMode(renderMode);
        }
    }

    public int getRenderMode() {
        return mGLThread != null ? mGLThread.getRenderMode() : RENDERMODE_CONTINUOUSLY;
    }

    public void requestRender() {
        if (mGLThread != null) {
            mGLThread.requestRender();
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (mGLThread != null) {
            mGLThread.surfaceCreated();
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        if (mGLThread != null) {
            mGLThread.onWindowResize(w, h);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (mGLThread != null) {
            mGLThread.surfaceDestroyed();
        }
    }

    @Override
    public void surfaceRedrawNeeded(SurfaceHolder holder) {
        if (mGLThread != null) {
            mGLThread.requestRender();
        }
    }

    public void surfaceRedrawNeededAsync(SurfaceHolder holder, Runnable finishDrawing) {
        if (mGLThread != null) {
            mGLThread.requestRender();
        }
        if (finishDrawing != null) {
            finishDrawing.run();
        }
    }

    public void onPause() {
        if (mGLThread != null) {
            mGLThread.onPause();
        }
    }

    public void onResume() {
        if (mGLThread != null) {
            mGLThread.onResume();
        }
    }

    public void queueEvent(Runnable r) {
        if (mGLThread != null) {
            mGLThread.queueEvent(r);
        }
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (mGLThread != null) {
            mGLThread.onResume();
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        if (mGLThread != null) {
            mGLThread.requestExitAndWait();
        }
        super.onDetachedFromWindow();
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            if (mGLThread != null) {
                mGLThread.requestExitAndWait();
            }
        } finally {
            super.finalize();
        }
    }

    private static native long nativeEglCreate(int clientVersion, int depthSize, int stencilSize);
    private static native boolean nativeEglMakeCurrent(long handle);
    private static native boolean nativeEglSwap(long handle);
    private static native void nativeEglDestroy(long handle);

    static class GLThread extends Thread {
        private final Object mLock = new Object();
        private final GLSurfaceView mView;
        private final Renderer mRenderer;
        private final int mClientVersion;
        private final int mDepthSize;
        private final int mStencilSize;
        private final ArrayList<Runnable> mEventQueue = new ArrayList<Runnable>();

        private volatile int mRenderMode = RENDERMODE_CONTINUOUSLY;
        private volatile boolean mRequestRender = false;
        private volatile boolean mPaused = false;
        private volatile boolean mHasSurface = false;
        private volatile boolean mExited = false;
        private volatile int mWidth = 0;
        private volatile int mHeight = 0;
        private boolean mSurfaceChangedPending = false;
        private long mEglState = 0;

        GLThread(GLSurfaceView view, Renderer renderer, int clientVersion, int depthSize, int stencilSize) {
            mView = view;
            mRenderer = renderer;
            mClientVersion = clientVersion;
            mDepthSize = depthSize;
            mStencilSize = stencilSize;
        }

        public void setRenderMode(int renderMode) {
            synchronized (mLock) {
                mRenderMode = renderMode;
                mLock.notifyAll();
            }
        }

        public int getRenderMode() {
            synchronized (mLock) {
                return mRenderMode;
            }
        }

        public void requestRender() {
            synchronized (mLock) {
                mRequestRender = true;
                mLock.notifyAll();
            }
        }

        public void surfaceCreated() {
            synchronized (mLock) {
                mHasSurface = true;
                mLock.notifyAll();
            }
        }

        public void onWindowResize(int w, int h) {
            synchronized (mLock) {
                mWidth = w;
                mHeight = h;
                mHasSurface = true;
                mSurfaceChangedPending = true;
                mLock.notifyAll();
            }
        }

        public void surfaceDestroyed() {
            synchronized (mLock) {
                mHasSurface = false;
                mLock.notifyAll();
            }
        }

        public void onPause() {
            synchronized (mLock) {
                mPaused = true;
                mLock.notifyAll();
            }
        }

        public void onResume() {
            synchronized (mLock) {
                mPaused = false;
                mLock.notifyAll();
            }
        }

        public void queueEvent(Runnable r) {
            if (r == null) return;
            synchronized (mLock) {
                mEventQueue.add(r);
                mLock.notifyAll();
            }
        }

        public void requestExitAndWait() {
            synchronized (mLock) {
                mExited = true;
                mLock.notifyAll();
            }
            try {
                join(500);
            } catch (InterruptedException ignored) {}
        }

        @Override
        public void run() {
            setName("GLThread");

            GL10 glProxy = null;
            try {
                glProxy = (GL10) Proxy.newProxyInstance(
                    GL10.class.getClassLoader(),
                    new Class<?>[]{GL10.class},
                    new InvocationHandler() {
                        @Override
                        public Object invoke(Object proxy, Method method, Object[] args) {
                            return null;
                        }
                    }
                );
            } catch (Throwable t) {
                Log.w(TAG, "Failed to create GL10 proxy: " + t);
            }

            final GL10 gl = glProxy;
            final EGLConfig config = new EGLConfig() {};

            while (true) {
                Runnable eventToRun = null;
                boolean needInit = false;
                boolean needResize = false;
                int w = 0, h = 0;

                synchronized (mLock) {
                    while (true) {
                        if (mExited) {
                            if (mEglState != 0) {
                                nativeEglDestroy(mEglState);
                                mEglState = 0;
                            }
                            return;
                        }

                        if (!mEventQueue.isEmpty()) {
                            eventToRun = mEventQueue.remove(0);
                            break;
                        }

                        if (mPaused || !mHasSurface || mWidth <= 0 || mHeight <= 0) {
                            try {
                                mLock.wait();
                            } catch (InterruptedException ignored) {}
                            continue;
                        }

                        if (mEglState == 0) {
                            needInit = true;
                            break;
                        }

                        if (mSurfaceChangedPending) {
                            needResize = true;
                            mSurfaceChangedPending = false;
                            w = mWidth;
                            h = mHeight;
                            break;
                        }

                        if (mRenderMode == RENDERMODE_CONTINUOUSLY || mRequestRender) {
                            mRequestRender = false;
                            break;
                        }

                        try {
                            mLock.wait();
                        } catch (InterruptedException ignored) {}
                    }
                }

                if (eventToRun != null) {
                    try {
                        eventToRun.run();
                    } catch (Throwable t) {
                        Log.e(TAG, "Exception running queued event: " + t);
                    }
                    continue;
                }

                if (needInit) {
                    mEglState = nativeEglCreate(mClientVersion, mDepthSize, mStencilSize);
                    if (mEglState != 0) {
                        nativeEglMakeCurrent(mEglState);
                        try {
                            mRenderer.onSurfaceCreated(gl, config);
                        } catch (Throwable t) {
                            Log.e(TAG, "onSurfaceCreated failed: " + t);
                        }
                        synchronized (mLock) {
                            mSurfaceChangedPending = true;
                        }
                    }
                    continue;
                }

                if (needResize) {
                    try {
                        mRenderer.onSurfaceChanged(gl, w, h);
                    } catch (Throwable t) {
                        Log.e(TAG, "onSurfaceChanged failed: " + t);
                    }
                    continue;
                }

                if (mEglState != 0) {
                    long t0 = System.nanoTime();
                    try {
                        mRenderer.onDrawFrame(gl);
                        nativeEglSwap(mEglState);
                    } catch (Throwable t) {
                        Log.e(TAG, "onDrawFrame failed: " + t);
                    }
                    long elapsedMs = (System.nanoTime() - t0) / 1000000L;
                    long sleepMs = 16L - elapsedMs;
                    if (sleepMs > 0) {
                        try {
                            Thread.sleep(sleepMs);
                        } catch (InterruptedException ignored) {}
                    }
                }
            }
        }
    }
}
