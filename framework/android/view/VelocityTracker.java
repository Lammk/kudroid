package android.view;

/**
 * Estimates pointer velocity from the recent movement samples, per pointer id. The
 * estimate is a least-squares line over the samples of the last 100 ms, which is the
 * platform's behaviour for a steady drag.
 */
public final class VelocityTracker {
    private static final int HISTORY = 20;
    private static final long HORIZON_MS = 100;
    private static final int MAX_POINTERS = 10;

    private static final java.util.ArrayList<VelocityTracker> sPool = new java.util.ArrayList<>();

    private final long[] mTime = new long[HISTORY];
    private final float[][] mX = new float[MAX_POINTERS][HISTORY];
    private final float[][] mY = new float[MAX_POINTERS][HISTORY];
    private final boolean[] mActive = new boolean[MAX_POINTERS];
    private final float[] mVx = new float[MAX_POINTERS];
    private final float[] mVy = new float[MAX_POINTERS];
    private int mCount;

    private VelocityTracker() {}

    public static VelocityTracker obtain() {
        synchronized (sPool) {
            if (!sPool.isEmpty()) return sPool.remove(sPool.size() - 1);
        }
        return new VelocityTracker();
    }

    public void recycle() {
        clear();
        synchronized (sPool) {
            if (sPool.size() < 4) sPool.add(this);
        }
    }

    public void clear() {
        mCount = 0;
        for (int i = 0; i < MAX_POINTERS; i++) {
            mActive[i] = false;
            mVx[i] = 0f;
            mVy[i] = 0f;
        }
    }

    public void addMovement(MotionEvent event) {
        if (event == null) throw new IllegalArgumentException("event must not be null");
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) clear();
        final int history = event.getHistorySize();
        for (int h = 0; h < history; h++) {
            push(event, h, event.getHistoricalEventTime(h));
        }
        push(event, -1, event.getEventTime());
    }

    private void push(MotionEvent event, int historyPos, long time) {
        if (mCount == HISTORY) {
            for (int i = 1; i < HISTORY; i++) {
                mTime[i - 1] = mTime[i];
                for (int p = 0; p < MAX_POINTERS; p++) {
                    mX[p][i - 1] = mX[p][i];
                    mY[p][i - 1] = mY[p][i];
                }
            }
            mCount--;
        }
        mTime[mCount] = time;
        for (int p = 0; p < MAX_POINTERS; p++) mActive[p] = false;
        for (int index = 0; index < event.getPointerCount(); index++) {
            final int id = event.getPointerId(index);
            if (id < 0 || id >= MAX_POINTERS) continue;
            mActive[id] = true;
            mX[id][mCount] = historyPos < 0 ? event.getX(index) : event.getHistoricalX(index, historyPos);
            mY[id][mCount] = historyPos < 0 ? event.getY(index) : event.getHistoricalY(index, historyPos);
        }
        mCount++;
    }

    public void computeCurrentVelocity(int units) {
        computeCurrentVelocity(units, Float.MAX_VALUE);
    }

    public void computeCurrentVelocity(int units, float maxVelocity) {
        for (int id = 0; id < MAX_POINTERS; id++) {
            if (!mActive[id] || mCount < 2) {
                mVx[id] = 0f;
                mVy[id] = 0f;
                continue;
            }
            mVx[id] = clamp(slope(mX[id]) * units, maxVelocity);
            mVy[id] = clamp(slope(mY[id]) * units, maxVelocity);
        }
    }

    /** Units per millisecond of a least-squares line through the recent samples. */
    private float slope(float[] values) {
        final long newest = mTime[mCount - 1];
        int first = mCount - 1;
        while (first > 0 && newest - mTime[first - 1] <= HORIZON_MS) first--;
        final int n = mCount - first;
        if (n < 2) return 0f;
        double sumT = 0, sumV = 0, sumTT = 0, sumTV = 0;
        for (int i = first; i < mCount; i++) {
            final double t = mTime[i] - newest;
            sumT += t;
            sumV += values[i];
            sumTT += t * t;
            sumTV += t * values[i];
        }
        final double denominator = n * sumTT - sumT * sumT;
        if (denominator == 0) return 0f;
        return (float) ((n * sumTV - sumT * sumV) / denominator);
    }

    private static float clamp(float v, float max) {
        if (v > max) return max;
        if (v < -max) return -max;
        return v;
    }

    public float getXVelocity() { return mVx[0]; }
    public float getYVelocity() { return mVy[0]; }

    public float getXVelocity(int id) {
        return id >= 0 && id < MAX_POINTERS ? mVx[id] : 0f;
    }

    public float getYVelocity(int id) {
        return id >= 0 && id < MAX_POINTERS ? mVy[id] : 0f;
    }
}
