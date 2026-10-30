package android.view;

import android.content.Context;
import android.os.Handler;

/**
 * Detects scaling transformation gestures using the supplied MotionEvents.
 */
public class ScaleGestureDetector {
    public interface OnScaleGestureListener {
        boolean onScale(ScaleGestureDetector detector);
        boolean onScaleBegin(ScaleGestureDetector detector);
        void onScaleEnd(ScaleGestureDetector detector);
    }

    public static class SimpleOnScaleGestureListener implements OnScaleGestureListener {
        public boolean onScale(ScaleGestureDetector detector) { return false; }
        public boolean onScaleBegin(ScaleGestureDetector detector) { return true; }
        public void onScaleEnd(ScaleGestureDetector detector) {}
    }

    private final Context mContext;
    private final OnScaleGestureListener mListener;
    private final Handler mHandler;
    private float mScaleFactor = 1.0f;
    private float mFocusX;
    private float mFocusY;
    private float mCurrentSpan;
    private float mPreviousSpan;
    private float mCurrentSpanX;
    private float mCurrentSpanY;
    private float mPreviousSpanX;
    private float mPreviousSpanY;
    private boolean mInProgress;
    private boolean mQuickScaleEnabled = true;
    private boolean mStylusScaleEnabled = true;
    private long mCurrTime;
    private long mPrevTime;

    public ScaleGestureDetector() {
        this(null, null, null);
    }

    public ScaleGestureDetector(Context context, OnScaleGestureListener listener) {
        this(context, listener, null);
    }

    public ScaleGestureDetector(Context context, OnScaleGestureListener listener, Handler handler) {
        mContext = context;
        mListener = listener;
        mHandler = handler;
    }

    public boolean onTouchEvent(MotionEvent event) {
        if (mListener == null || event == null) return false;
        final int action = event.getActionMasked();
        final int count = event.getPointerCount();

        if (action == MotionEvent.ACTION_DOWN ||
            action == MotionEvent.ACTION_POINTER_DOWN ||
            action == MotionEvent.ACTION_POINTER_UP ||
            action == MotionEvent.ACTION_UP ||
            action == MotionEvent.ACTION_CANCEL) {
            if (mInProgress && (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL || count < 2)) {
                mInProgress = false;
                mListener.onScaleEnd(this);
            }
            return true;
        }

        if (action == MotionEvent.ACTION_MOVE && count >= 2) {
            float sumX = 0, sumY = 0;
            for (int i = 0; i < count; i++) {
                sumX += event.getX(i);
                sumY += event.getY(i);
            }
            mFocusX = sumX / count;
            mFocusY = sumY / count;

            float devX = 0, devY = 0;
            for (int i = 0; i < count; i++) {
                devX += Math.abs(event.getX(i) - mFocusX);
                devY += Math.abs(event.getY(i) - mFocusY);
            }
            float spanX = (devX / count) * 2;
            float spanY = (devY / count) * 2;
            float span = (float) Math.hypot(spanX, spanY);

            if (!mInProgress) {
                mCurrentSpan = span;
                mPreviousSpan = span;
                mCurrentSpanX = spanX;
                mCurrentSpanY = spanY;
                mPreviousSpanX = spanX;
                mPreviousSpanY = spanY;
                mInProgress = mListener.onScaleBegin(this);
            } else {
                mPreviousSpan = mCurrentSpan;
                mPreviousSpanX = mCurrentSpanX;
                mPreviousSpanY = mCurrentSpanY;
                mCurrentSpan = span;
                mCurrentSpanX = spanX;
                mCurrentSpanY = spanY;
                if (mPreviousSpan > 0) {
                    mScaleFactor = mCurrentSpan / mPreviousSpan;
                } else {
                    mScaleFactor = 1.0f;
                }
                mPrevTime = mCurrTime;
                mCurrTime = event.getEventTime();
                mListener.onScale(this);
            }
            return true;
        }
        return false;
    }

    public boolean isInProgress() { return mInProgress; }
    public float getFocusX() { return mFocusX; }
    public float getFocusY() { return mFocusY; }
    public float getCurrentSpan() { return mCurrentSpan; }
    public float getCurrentSpanX() { return mCurrentSpanX; }
    public float getCurrentSpanY() { return mCurrentSpanY; }
    public float getPreviousSpan() { return mPreviousSpan; }
    public float getPreviousSpanX() { return mPreviousSpanX; }
    public float getPreviousSpanY() { return mPreviousSpanY; }
    public float getScaleFactor() { return mScaleFactor; }
    public long getTimeDelta() { return mCurrTime - mPrevTime; }
    public long getEventTime() { return mCurrTime; }

    public void setQuickScaleEnabled(boolean scales) { mQuickScaleEnabled = scales; }
    public boolean isQuickScaleEnabled() { return mQuickScaleEnabled; }
    public void setStylusScaleEnabled(boolean scales) { mStylusScaleEnabled = scales; }
    public boolean isStylusScaleEnabled() { return mStylusScaleEnabled; }
}
