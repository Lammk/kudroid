package android.graphics;

import java.util.ArrayList;

/**
 * Measures the contours of a Path: length, position and tangent at a distance, and
 * sub-segments. Curves are measured through a fine line approximation.
 */
public class PathMeasure {
    public static final int POSITION_MATRIX_FLAG = 0x01;
    public static final int TANGENT_MATRIX_FLAG = 0x02;

    private static final float TOLERANCE = 0.05f;

    private Path mPath;
    private boolean mForceClosed;
    // (cumulative length, x, y) triples for the whole path and where each contour starts.
    private float[] mFlat = new float[0];
    private final ArrayList<Integer> mStarts = new ArrayList<Integer>();
    private final ArrayList<Boolean> mClosed = new ArrayList<Boolean>();
    private int mContour = -1;

    public PathMeasure() {
    }

    public PathMeasure(Path path, boolean forceClosed) {
        setPath(path, forceClosed);
    }

    public void setPath(Path path, boolean forceClosed) {
        mPath = path;
        mForceClosed = forceClosed;
        mStarts.clear();
        mClosed.clear();
        mFlat = path != null ? path.flatten(TOLERANCE, mStarts, mClosed) : new float[0];
        mContour = mStarts.isEmpty() ? -1 : 0;
    }

    private int first() {
        return mStarts.get(mContour) * 3;
    }

    private int last() {
        return (mContour + 1 < mStarts.size() ? mStarts.get(mContour + 1) * 3 : mFlat.length) - 3;
    }

    private float base() {
        return mFlat[first()];
    }

    private boolean closedByPath() {
        return mClosed.get(mContour);
    }

    /** Length of the segment forceClosed adds when the contour is not already closed. */
    private float closingLength() {
        final int f = first();
        final int l = last();
        if (!mForceClosed || closedByPath() || l <= f) {
            return 0f;
        }
        return (float) Math.hypot(mFlat[f + 1] - mFlat[l + 1], mFlat[f + 2] - mFlat[l + 2]);
    }

    /** Length of the current contour. */
    public float getLength() {
        if (mContour < 0) {
            return 0f;
        }
        return mFlat[last()] - base() + closingLength();
    }

    public boolean isClosed() {
        if (mContour < 0) {
            return false;
        }
        return mForceClosed || closedByPath();
    }

    /** Moves to the next contour; false when there is none. */
    public boolean nextContour() {
        if (mContour < 0 || mContour + 1 >= mStarts.size()) {
            mContour = -1;
            return false;
        }
        mContour++;
        return true;
    }

    private float[] pointAt(float distance) {
        final int f = first();
        final int l = last();
        final float total = getLength();
        float d = Math.max(0f, Math.min(distance, total)) + base();
        final float pathEnd = mFlat[l];
        if (d > pathEnd) {
            // On the implicit closing segment.
            final float t = closingLength() > 0f ? (d - pathEnd) / closingLength() : 0f;
            final float x = mFlat[l + 1] + (mFlat[f + 1] - mFlat[l + 1]) * t;
            final float y = mFlat[l + 2] + (mFlat[f + 2] - mFlat[l + 2]) * t;
            return new float[] {x, y, mFlat[f + 1] - mFlat[l + 1], mFlat[f + 2] - mFlat[l + 2]};
        }
        // Binary search for the segment holding d.
        int lo = f / 3;
        int hi = l / 3;
        while (hi - lo > 1) {
            final int mid = (lo + hi) >>> 1;
            if (mFlat[mid * 3] <= d) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        // Skip zero-length segments so the tangent is defined.
        int a = lo;
        int b = Math.min(lo + 1, l / 3);
        while (b < l / 3 && mFlat[b * 3] == mFlat[a * 3]) {
            b++;
        }
        final float segment = mFlat[b * 3] - mFlat[a * 3];
        final float t = segment > 0f ? (d - mFlat[a * 3]) / segment : 0f;
        final float x = mFlat[a * 3 + 1] + (mFlat[b * 3 + 1] - mFlat[a * 3 + 1]) * t;
        final float y = mFlat[a * 3 + 2] + (mFlat[b * 3 + 2] - mFlat[a * 3 + 2]) * t;
        return new float[] {x, y, mFlat[b * 3 + 1] - mFlat[a * 3 + 1],
                mFlat[b * 3 + 2] - mFlat[a * 3 + 2]};
    }

    /** Position and unit tangent at a distance along the contour. */
    public boolean getPosTan(float distance, float[] pos, float[] tan) {
        if ((pos != null && pos.length < 2) || (tan != null && tan.length < 2)) {
            throw new ArrayIndexOutOfBoundsException();
        }
        if (mContour < 0 || distance < 0f || distance > getLength()) {
            return false;
        }
        final float[] p = pointAt(distance);
        if (pos != null) {
            pos[0] = p[0];
            pos[1] = p[1];
        }
        if (tan != null) {
            final float len = (float) Math.hypot(p[2], p[3]);
            tan[0] = len > 0f ? p[2] / len : 1f;
            tan[1] = len > 0f ? p[3] / len : 0f;
        }
        return true;
    }

    public boolean getMatrix(float distance, Matrix matrix, int flags) {
        final float[] pos = new float[2];
        final float[] tan = new float[2];
        if (!getPosTan(distance, pos, tan)) {
            return false;
        }
        if ((flags & TANGENT_MATRIX_FLAG) != 0) {
            matrix.setSinCos(tan[1], tan[0]);
        } else {
            matrix.reset();
        }
        if ((flags & POSITION_MATRIX_FLAG) != 0) {
            matrix.postTranslate(pos[0], pos[1]);
        }
        return true;
    }

    /** Appends the part of the contour between two distances to dst. */
    public boolean getSegment(float startD, float stopD, Path dst, boolean startWithMoveTo) {
        final float total = getLength();
        if (mContour < 0 || total <= 0f) {
            return false;
        }
        startD = Math.max(0f, startD);
        stopD = Math.min(stopD, total);
        if (startD >= stopD) {
            return false;
        }
        final float[] s = pointAt(startD);
        if (startWithMoveTo) {
            dst.moveTo(s[0], s[1]);
        }
        final float base = base();
        final int f = first();
        final int l = last();
        final float pathEnd = mFlat[l];
        for (int i = f / 3 + 1; i <= l / 3; i++) {
            final float d = mFlat[i * 3] - base;
            if (d <= startD) {
                continue;
            }
            if (d >= stopD) {
                break;
            }
            dst.lineTo(mFlat[i * 3 + 1], mFlat[i * 3 + 2]);
        }
        if (stopD + base > pathEnd) {
            dst.lineTo(mFlat[l + 1], mFlat[l + 2]);
        }
        final float[] e = pointAt(stopD);
        dst.lineTo(e[0], e[1]);
        return true;
    }
}
