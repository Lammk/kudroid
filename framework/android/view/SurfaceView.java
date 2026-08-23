package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;

/**
 * Stub tối thiểu của android.view.SurfaceView cho KuDroid.
 *
 * Pattern chuẩn game Android:
 *   class MainActivity extends SurfaceView implements SurfaceHolder.Callback2
 * MainActivity của MCPE kế thừa class này nên JVM cần resolve toàn bộ hierarchy
 * khi load. onDraw là no-op vì rendering thật chạy qua native Metal pipeline.
 */
public class SurfaceView extends View implements SurfaceHolder.Callback2 {

    private SurfaceHolder mHolder;

    public SurfaceView(Context context) {
        super(context);
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
            for (Object obj : cbs) {
                if (obj instanceof Callback) {
                    Callback cb = (Callback) obj;
                    try {
                        cb.surfaceCreated(this);
                        cb.surfaceChanged(this, 0, 1080, 1920);
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
            synchronized (mCallbacks) {
                if (!mCallbacks.contains(callback)) {
                    mCallbacks.add(callback);
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
            int w = mView != null ? mView.getWidth() : 1080;
            int h = mView != null ? mView.getHeight() : 1920;
            return new Rect(0, 0, w > 0 ? w : 1080, h > 0 ? h : 1920);
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
