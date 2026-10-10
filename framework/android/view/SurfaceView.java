package android.view;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;

/** A lightweight SurfaceView whose holder follows the laid out host surface. */
public class SurfaceView extends View implements SurfaceHolder.Callback2 {

    private final SimpleSurfaceHolder mHolder;

    public SurfaceView(Context context) {
        this(context, null);
    }

    public SurfaceView(Context context, android.util.AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public SurfaceView(Context context, android.util.AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public SurfaceView(Context context, android.util.AttributeSet attrs, int defStyleAttr,
                       int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        mHolder = new SimpleSurfaceHolder(this);
    }

    public SurfaceHolder getHolder() {
        return mHolder;
    }

    public void setZOrderMediaOverlay(boolean isMediaOverlay) {}
    public void setZOrderOnTop(boolean onTop) {}

    @Override public void surfaceCreated(SurfaceHolder holder) {}
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}
    @Override public void surfaceDestroyed(SurfaceHolder holder) {}
    @Override public void surfaceRedrawNeeded(SurfaceHolder holder) {}

    /** Called by Window after the view has been measured against the real framebuffer. */
    public void dispatchSurfaceReady(int width, int height) {
        if (width <= 0 || height <= 0) {
            try {
                android.graphics.Canvas canvas = new android.graphics.Canvas();
                width = canvas.getWidth();
                height = canvas.getHeight();
            } catch (Throwable ignored) {}
        }
        if (width <= 0) width = 1080;
        if (height <= 0) height = 1920;
        mHolder.dispatchSurfaceReady(width, height);
    }

    public void dispatchSurfaceCreated() {
        dispatchSurfaceReady(getWidth(), getHeight());
    }

    /** Called before the owning Activity is destroyed or its host surface is lost. */
    public void dispatchSurfaceDestroyed() {
        mHolder.dispatchSurfaceDestroyed();
    }

    @Override
    protected void onDraw(Canvas canvas) {}

    private static final class CallbackState {
        final SurfaceHolder.Callback callback;
        boolean created;
        int width;
        int height;

        CallbackState(SurfaceHolder.Callback callback) {
            this.callback = callback;
        }
    }

    private static final class SimpleSurfaceHolder implements SurfaceHolder {
        private final SurfaceView mView;
        private final Surface mSurface = new Surface();
        private final java.util.ArrayList<CallbackState> mCallbacks =
                new java.util.ArrayList<CallbackState>();
        private volatile boolean mCreated;
        private volatile int mWidth;
        private volatile int mHeight;

        SimpleSurfaceHolder(SurfaceView view) {
            mView = view;
        }

        void dispatchSurfaceReady(int width, int height) {
            if (mView == null || !mView.isLaidOut() || width <= 0 || height <= 0) return;
            mCreated = true;
            mWidth = width;
            mHeight = height;
            mSurface.setSurfaceSize(width, height);
            CallbackState[] callbacks;
            synchronized (mCallbacks) {
                callbacks = mCallbacks.toArray(new CallbackState[mCallbacks.size()]);
            }
            for (CallbackState state : callbacks) dispatchToCallback(state);
        }

        void dispatchSurfaceDestroyed() {
            if (!mCreated) {
                mSurface.clearSurface();
                mWidth = 0;
                mHeight = 0;
                return;
            }
            mCreated = false;
            mSurface.clearSurface();
            mWidth = 0;
            mHeight = 0;
            CallbackState[] callbacks;
            synchronized (mCallbacks) {
                callbacks = mCallbacks.toArray(new CallbackState[mCallbacks.size()]);
            }
            for (CallbackState state : callbacks) {
                boolean notify;
                synchronized (mCallbacks) {
                    notify = state.created && mCallbacks.contains(state);
                    state.created = false;
                    state.width = 0;
                    state.height = 0;
                }
                if (notify) invokeDestroyed(state.callback);
            }
        }

        private void dispatchToCallback(CallbackState state) {
            boolean created;
            boolean changed;
            final int width;
            final int height;
            synchronized (mCallbacks) {
                if (!mCreated || !mCallbacks.contains(state)) return;
                created = !state.created;
                changed = created || state.width != mWidth || state.height != mHeight;
                state.created = true;
                state.width = mWidth;
                state.height = mHeight;
                width = mWidth;
                height = mHeight;
            }
            if (created) invokeCreated(state.callback);
            if (changed) invokeChanged(state.callback, width, height);
            if (changed && state.callback instanceof SurfaceHolder.Callback2) {
                invokeRedraw((SurfaceHolder.Callback2) state.callback);
            }
        }

        @Override
        public void addCallback(SurfaceHolder.Callback callback) {
            if (callback == null) return;
            final CallbackState state;
            synchronized (mCallbacks) {
                for (CallbackState existing : mCallbacks) {
                    if (existing.callback == callback) return;
                }
                state = new CallbackState(callback);
                mCallbacks.add(state);
            }
            if (mCreated) {
                mView.post(new Runnable() {
                    @Override public void run() { dispatchToCallback(state); }
                });
            }
        }

        @Override
        public void removeCallback(SurfaceHolder.Callback callback) {
            if (callback == null) return;
            synchronized (mCallbacks) {
                for (int i = mCallbacks.size() - 1; i >= 0; i--) {
                    if (mCallbacks.get(i).callback == callback) mCallbacks.remove(i);
                }
            }
        }

        private void invokeCreated(SurfaceHolder.Callback callback) {
            try { callback.surfaceCreated(this); }
            catch (Throwable t) { android.util.Log.e("SurfaceHolder", "surfaceCreated failed: " + t); }
        }

        private void invokeChanged(SurfaceHolder.Callback callback, int width, int height) {
            try { callback.surfaceChanged(this, 0, width, height); }
            catch (Throwable t) { android.util.Log.e("SurfaceHolder", "surfaceChanged failed: " + t); }
        }

        private void invokeRedraw(SurfaceHolder.Callback2 callback) {
            try { callback.surfaceRedrawNeeded(this); }
            catch (Throwable t) { android.util.Log.e("SurfaceHolder", "surfaceRedrawNeeded failed: " + t); }
        }

        private void invokeDestroyed(SurfaceHolder.Callback callback) {
            try { callback.surfaceDestroyed(this); }
            catch (Throwable t) { android.util.Log.e("SurfaceHolder", "surfaceDestroyed failed: " + t); }
        }

        @Override public Surface getSurface() { return mSurface; }

        @Override
        public Rect getSurfaceFrame() {
            return mCreated ? new Rect(0, 0, mWidth, mHeight) : new Rect(0, 0, 0, 0);
        }

        @Override public boolean isCreating() { return false; }
        @Override public void setType(int type) {}
        @Override public void setFixedSize(int width, int height) {}
        @Override public void setSizeFromLayout() {}
        @Override public void setFormat(int format) {}
        @Override public void setKeepScreenOn(boolean screenOn) {}
        @Override public Canvas lockCanvas() { return mSurface.lockCanvas(); }
        @Override public Canvas lockCanvas(Rect dirty) { return mSurface.lockCanvas(dirty); }
        @Override public void unlockCanvasAndPost(Canvas canvas) { mSurface.unlockCanvasAndPost(canvas); }
        @Override public Canvas lockCanvasAndroidOnly(Rect dirty) { return mSurface.lockCanvas(dirty); }
    }
}
