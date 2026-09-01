package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Minimum stub of android.view.SurfaceView for KuDroid.
 *
 * Android game standard pattern:
 *   class MainActivity extends SurfaceView implements SurfaceHolder.Callback2
 * MCPE's MainActivity inherits this class so the JVM needs to resolve the entire hierarchy
 * when loading. onDraw is a no-op because the actual rendering runs through the native Metal pipeline.
 */
public class SurfaceView extends View implements SurfaceHolder.Callback2 {

    private SurfaceHolder mHolder;

    public SurfaceView(Context context) {
        this(context, null);
    }

    public SurfaceView(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public SurfaceView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public SurfaceView(Context context, android.util.AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        init();
    }

    private void init() {
        mHolder = new SimpleSurfaceHolder(this);
    }

    public SurfaceHolder getHolder() {
        if (mHolder == null) {
            mHolder = new SimpleSurfaceHolder(this);
        }
        return mHolder;
    }

    public void setZOrderMediaOverlay(boolean isMediaOverlay) {}
    public void setZOrderOnTop(boolean onTop) {}

    @Override
    public void surfaceCreated(SurfaceHolder holder) {}

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {}

    @Override
    public void surfaceRedrawNeeded(SurfaceHolder holder) {}

    public void dispatchSurfaceCreated() {
        if (mHolder instanceof SimpleSurfaceHolder) {
            ((SimpleSurfaceHolder) mHolder).dispatchSurfaceCreated();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {}

    private static class SimpleSurfaceHolder implements SurfaceHolder {
        private final View mView;
        private final Surface mSurface = new Surface();
        private final java.util.ArrayList<Callback> mCallbacks = new java.util.ArrayList<Callback>();

        public SimpleSurfaceHolder(View view) {
            mView = view;
        }

        public void dispatchSurfaceCreated() {
            Object[] cbs;
            synchronized (mCallbacks) {
                cbs = mCallbacks.toArray();
            }
            int w = mView != null ? mView.getWidth() : 0;
            int h = mView != null ? mView.getHeight() : 0;
            if (w <= 0 || h <= 0) {
                try {
                    android.graphics.Canvas canvas = new android.graphics.Canvas();
                    w = canvas.getWidth();
                    h = canvas.getHeight();
                } catch (Throwable ignored) {}
            }
            for (Object obj : cbs) {
                if (obj instanceof Callback) {
                    Callback cb = (Callback) obj;
                    try {
                        cb.surfaceCreated(this);
                        cb.surfaceChanged(this, 0, w, h);
                        if (cb instanceof Callback2) {
                            ((Callback2) cb).surfaceRedrawNeeded(this);
                        }
                    } catch (Throwable t) {
                        android.util.Log.e("SurfaceHolder", "Error in dispatchSurfaceCreated: " + t);
                    }
                }
            }
        }

        @Override
        public void addCallback(Callback callback) {
            if (callback == null) return;
            boolean isNew = false;
            synchronized (mCallbacks) {
                if (!mCallbacks.contains(callback)) {
                    mCallbacks.add(callback);
                    isNew = true;
                }
            }
            if (isNew) {
                int w = mView != null ? mView.getWidth() : 0;
                int h = mView != null ? mView.getHeight() : 0;
                if (w <= 0 || h <= 0) {
                    try {
                        android.graphics.Canvas canvas = new android.graphics.Canvas();
                        w = canvas.getWidth();
                        h = canvas.getHeight();
                    } catch (Throwable ignored) {}
                }
                if (w <= 0) w = 1080;
                if (h <= 0) h = 1920;
                try {
                    callback.surfaceCreated(this);
                    callback.surfaceChanged(this, 0, w, h);
                    if (callback instanceof Callback2) {
                        ((Callback2) callback).surfaceRedrawNeeded(this);
                    }
                } catch (Throwable t) {
                    android.util.Log.e("SurfaceHolder", "Error in immediate addCallback surface dispatch: " + t);
                }
            }
        }

        @Override
        public void removeCallback(Callback callback) {
            if (callback == null) return;
            synchronized (mCallbacks) {
                mCallbacks.remove(callback);
            }
        }

        @Override
        public Surface getSurface() {
            return mSurface;
        }

        @Override
        public Rect getSurfaceFrame() {
            int w = mView != null ? mView.getWidth() : 0;
            int h = mView != null ? mView.getHeight() : 0;
            if (w <= 0 || h <= 0) {
                try {
                    android.graphics.Canvas canvas = new android.graphics.Canvas();
                    w = canvas.getWidth();
                    h = canvas.getHeight();
                } catch (Throwable ignored) {}
            }
            return new Rect(0, 0, w, h);
        }

        @Override
        public boolean isCreating() { return false; }
        @Override
        public void setType(int type) {}
        @Override
        public void setFixedSize(int width, int height) {}
        @Override
        public void setSizeFromLayout() {}
        @Override
        public void setFormat(int format) {}
        @Override
        public void setKeepScreenOn(boolean screenOn) {}

        @Override
        public Canvas lockCanvas() { return mSurface.lockCanvas(); }
        @Override
        public Canvas lockCanvas(Rect dirty) { return mSurface.lockCanvas(dirty); }
        @Override
        public void unlockCanvasAndPost(Canvas canvas) { mSurface.unlockCanvasAndPost(canvas); }
        @Override
        public Canvas lockCanvasAndroidOnly(Rect dirty) { return mSurface.lockCanvas(dirty); }
    }
}
