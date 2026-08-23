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

    private final java.util.List<SurfaceHolder.Callback> mCallbacks =
        new java.util.concurrent.CopyOnWriteArrayList<SurfaceHolder.Callback>();

    private final SurfaceHolder mHolder = new SurfaceHolder() {
        private Surface mSurface = new Surface();

        @Override
        public void addCallback(Callback callback) {
            if (callback != null && !mCallbacks.contains(callback)) {
                mCallbacks.add(callback);
                try {
                    android.util.Log.i("SurfaceView", "dispatching surfaceCreated & surfaceChanged to " + callback.getClass().getName());
                    callback.surfaceCreated(this);
                    callback.surfaceChanged(this, 0, 1080, 1920);
                    if (callback instanceof Callback2) {
                        ((Callback2) callback).surfaceRedrawNeeded(this);
                    }
                } catch (Throwable t) {
                    android.util.Log.e("SurfaceView", "Error in surface callback: " + t);
                }
            }
        }

        @Override
        public void removeCallback(Callback callback) {
            if (callback != null) {
                mCallbacks.remove(callback);
            }
        }

        @Override
        public Surface getSurface() {
            return mSurface;
        }

        @Override
        public Rect getSurfaceFrame() {
            return new Rect(0, 0, getWidth(), getHeight());
        }

        @Override
        public boolean isCreating() {
            return false;
        }

        @Override
        public void setType(int type) {
        }

        @Override
        public void setFixedSize(int width, int height) {
        }

        @Override
        public void setSizeFromLayout() {
        }

        @Override
        public void setFormat(int format) {
        }

        @Override
        public void setKeepScreenOn(boolean screenOn) {
        }

        @Override
        public Canvas lockCanvas() {
            return mSurface.lockCanvas();
        }

        @Override
        public Canvas lockCanvas(Rect dirty) {
            return mSurface.lockCanvas(dirty);
        }

        @Override
        public void unlockCanvasAndPost(Canvas canvas) {
            mSurface.unlockCanvasAndPost(canvas);
        }

        @Override
        public Canvas lockCanvasAndroidOnly(Rect dirty) {
            return mSurface.lockCanvas(dirty);
        }
    };

    public SurfaceView(Context context) {
        super(context);
    }

    /** Trả về holder quản lý surface của view này. */
    public SurfaceHolder getHolder() {
        return mHolder;
    }

    /** Điều chỉnh z-order so với window (game overlay). */
    public void setZOrderMediaOverlay(boolean isMediaOverlay) {
    }

    public void setZOrderOnTop(boolean onTop) {
    }

    // ── SurfaceHolder.Callback2 ────────────────────────────────────────────
    // No-op mặc định; MainActivity override các method này.

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
    }

    @Override
    public void surfaceRedrawNeeded(SurfaceHolder holder) {
    }

    public void dispatchSurfaceCreated() {
        for (SurfaceHolder.Callback cb : mCallbacks) {
            try {
                cb.surfaceCreated(mHolder);
                cb.surfaceChanged(mHolder, 0, 1080, 1920);
                if (cb instanceof SurfaceHolder.Callback2) {
                    ((SurfaceHolder.Callback2) cb).surfaceRedrawNeeded(mHolder);
                }
            } catch (Throwable t) {
                android.util.Log.e("SurfaceView", "Error in dispatchSurfaceCreated: " + t);
            }
        }
    }

    public void dispatchSurfaceChanged(int width, int height) {
        for (SurfaceHolder.Callback cb : mCallbacks) {
            try {
                cb.surfaceChanged(mHolder, 0, width, height);
            } catch (Throwable t) {
                android.util.Log.e("SurfaceView", "Error in dispatchSurfaceChanged: " + t);
            }
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        // Rendering thật do native đảm nhiệm.
    }
}
