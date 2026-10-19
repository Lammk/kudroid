package android.graphics;

/**
 * The Path class encapsulates compound (multiple contour) geometric paths made of lines,
 * quadratic and cubic Bezier segments, following the AOSP API.
 *
 * Contours are stored as verbs plus coordinates. Arcs and ovals are converted to cubic
 * segments when they are added, so every consumer sees the same four segment kinds.
 */
public class Path {

    public enum FillType {
        WINDING(0),
        EVEN_ODD(1),
        INVERSE_WINDING(2),
        INVERSE_EVEN_ODD(3);

        FillType(int ni) {
            nativeInt = ni;
        }
        final int nativeInt;
    }

    public enum Direction {
        CW(0),
        CCW(1);

        Direction(int ni) {
            nativeInt = ni;
        }
        final int nativeInt;
    }

    public enum Op {
        DIFFERENCE,
        INTERSECT,
        UNION,
        XOR,
        REVERSE_DIFFERENCE
    }

    static final byte VERB_MOVE = 0;
    static final byte VERB_LINE = 1;
    static final byte VERB_QUAD = 2;
    static final byte VERB_CUBIC = 3;
    static final byte VERB_CLOSE = 4;

    /** Control points a verb consumes. */
    static int pointsFor(byte verb) {
        switch (verb) {
            case VERB_MOVE:
            case VERB_LINE:
                return 1;
            case VERB_QUAD:
                return 2;
            case VERB_CUBIC:
                return 3;
            default:
                return 0;
        }
    }

    // Circle quarter-arc control distance.
    private static final float KAPPA = 0.5522847498f;

    private FillType mFillType = FillType.WINDING;
    byte[] mVerbs = new byte[8];
    int mVerbCount;
    float[] mCoords = new float[16];
    int mCoordCount;
    private float mLastX;
    private float mLastY;
    private float mMoveX;
    private float mMoveY;
    private boolean mHasCurrent;

    public Path() {
    }

    public Path(Path src) {
        set(src);
    }

    private void addVerb(byte verb) {
        if (mVerbCount == mVerbs.length) {
            mVerbs = java.util.Arrays.copyOf(mVerbs, mVerbCount * 2);
        }
        mVerbs[mVerbCount++] = verb;
    }

    private void addPoint(float x, float y) {
        if (mCoordCount + 2 > mCoords.length) {
            mCoords = java.util.Arrays.copyOf(mCoords, mCoords.length * 2);
        }
        mCoords[mCoordCount++] = x;
        mCoords[mCoordCount++] = y;
        mLastX = x;
        mLastY = y;
    }

    /** Starts a contour at the current point when none is open, as Skia does implicitly. */
    private void ensureStart(float x, float y) {
        if (!mHasCurrent) {
            moveTo(x, y);
        }
    }

    public void reset() {
        rewind();
        mFillType = FillType.WINDING;
    }

    public void rewind() {
        mVerbCount = 0;
        mCoordCount = 0;
        mLastX = mLastY = mMoveX = mMoveY = 0;
        mHasCurrent = false;
    }

    public void set(Path src) {
        if (this == src) {
            return;
        }
        rewind();
        if (src == null) {
            return;
        }
        mFillType = src.mFillType;
        mVerbs = java.util.Arrays.copyOf(src.mVerbs, Math.max(src.mVerbCount, 8));
        mVerbCount = src.mVerbCount;
        mCoords = java.util.Arrays.copyOf(src.mCoords, Math.max(src.mCoordCount, 16));
        mCoordCount = src.mCoordCount;
        mLastX = src.mLastX;
        mLastY = src.mLastY;
        mMoveX = src.mMoveX;
        mMoveY = src.mMoveY;
        mHasCurrent = src.mHasCurrent;
    }

    public FillType getFillType() {
        return mFillType;
    }

    public void setFillType(FillType ft) {
        mFillType = (ft != null) ? ft : FillType.WINDING;
    }

    public boolean isInverseFillType() {
        return mFillType == FillType.INVERSE_WINDING || mFillType == FillType.INVERSE_EVEN_ODD;
    }

    public void toggleInverseFillType() {
        switch (mFillType) {
            case WINDING: mFillType = FillType.INVERSE_WINDING; break;
            case EVEN_ODD: mFillType = FillType.INVERSE_EVEN_ODD; break;
            case INVERSE_WINDING: mFillType = FillType.WINDING; break;
            default: mFillType = FillType.EVEN_ODD; break;
        }
    }

    public boolean isEmpty() {
        return mVerbCount == 0;
    }

    /** True when the path is a single axis-aligned rectangle; rect receives its bounds. */
    public boolean isRect(RectF rect) {
        if (mVerbCount < 4 || mVerbs[0] != VERB_MOVE) {
            return false;
        }
        final float[] p = new float[10];
        p[0] = mCoords[0];
        p[1] = mCoords[1];
        int n = 1;
        int ci = 2;
        for (int i = 1; i < mVerbCount; i++) {
            if (mVerbs[i] == VERB_LINE) {
                if (n >= 5) {
                    return false;
                }
                p[2 * n] = mCoords[ci++];
                p[2 * n + 1] = mCoords[ci++];
                n++;
            } else if (mVerbs[i] == VERB_CLOSE && i == mVerbCount - 1) {
                break;
            } else {
                return false;
            }
        }
        if (n == 5) {
            // An explicit line back to the start is the same rectangle.
            if (p[8] != p[0] || p[9] != p[1]) {
                return false;
            }
            n = 4;
        }
        if (n != 4) {
            return false;
        }
        // Each edge is axis aligned and horizontal and vertical edges alternate.
        final boolean firstHorizontal = p[1] == p[3];
        for (int k = 0; k < 4; k++) {
            final int a = k;
            final int b = (k + 1) % 4;
            final boolean horizontal = p[2 * a + 1] == p[2 * b + 1];
            final boolean vertical = p[2 * a] == p[2 * b];
            if (horizontal == vertical || horizontal != (firstHorizontal == (k % 2 == 0))) {
                return false;
            }
        }
        if (rect != null) {
            rect.set(Math.min(Math.min(p[0], p[2]), Math.min(p[4], p[6])),
                    Math.min(Math.min(p[1], p[3]), Math.min(p[5], p[7])),
                    Math.max(Math.max(p[0], p[2]), Math.max(p[4], p[6])),
                    Math.max(Math.max(p[1], p[3]), Math.max(p[5], p[7])));
        }
        return true;
    }

    /** A path made only of lines is convex when every turn has the same direction. */
    public boolean isConvex() {
        float prevDx = 0, prevDy = 0, firstDx = 0, firstDy = 0;
        float x = 0, y = 0, startX = 0, startY = 0;
        int sign = 0;
        int ci = 0;
        boolean haveEdge = false;
        for (int v = 0; v < mVerbCount; v++) {
            float nx;
            float ny;
            switch (mVerbs[v]) {
                case VERB_MOVE:
                    if (v != 0) {
                        return false;
                    }
                    x = startX = mCoords[ci++];
                    y = startY = mCoords[ci++];
                    continue;
                case VERB_LINE:
                    nx = mCoords[ci++];
                    ny = mCoords[ci++];
                    break;
                case VERB_CLOSE:
                    nx = startX;
                    ny = startY;
                    break;
                default:
                    return false;
            }
            final float dx = nx - x;
            final float dy = ny - y;
            x = nx;
            y = ny;
            if (dx == 0 && dy == 0) {
                continue;
            }
            if (haveEdge) {
                final float cross = prevDx * dy - prevDy * dx;
                if (cross != 0) {
                    final int s = cross > 0 ? 1 : -1;
                    if (sign != 0 && s != sign) {
                        return false;
                    }
                    sign = s;
                }
            } else {
                firstDx = dx;
                firstDy = dy;
                haveEdge = true;
            }
            prevDx = dx;
            prevDy = dy;
        }
        if (haveEdge && (x != startX || y != startY)) {
            return false;
        }
        if (haveEdge) {
            final float cross = prevDx * firstDy - prevDy * firstDx;
            if (cross != 0 && sign != 0 && (cross > 0 ? 1 : -1) != sign) {
                return false;
            }
        }
        return true;
    }

    /**
     * Bounds of the path. The exact form follows the curves; the other uses the control
     * points, which is what the platform's fast path returns.
     */
    public void computeBounds(RectF bounds, boolean exact) {
        if (bounds == null) {
            return;
        }
        if (mCoordCount == 0) {
            bounds.setEmpty();
            return;
        }
        float l = Float.MAX_VALUE, t = Float.MAX_VALUE, r = -Float.MAX_VALUE, b = -Float.MAX_VALUE;
        if (!exact) {
            for (int i = 0; i < mCoordCount; i += 2) {
                l = Math.min(l, mCoords[i]);
                r = Math.max(r, mCoords[i]);
                t = Math.min(t, mCoords[i + 1]);
                b = Math.max(b, mCoords[i + 1]);
            }
        } else {
            final float[] flat = flatten(0.05f, null, null);
            for (int i = 0; i < flat.length; i += 3) {
                l = Math.min(l, flat[i + 1]);
                r = Math.max(r, flat[i + 1]);
                t = Math.min(t, flat[i + 2]);
                b = Math.max(b, flat[i + 2]);
            }
            if (flat.length == 0) {
                bounds.setEmpty();
                return;
            }
        }
        bounds.set(l, t, r, b);
    }

    public void incReserve(int extraPtCount) {
        if (extraPtCount > 0 && mCoordCount + extraPtCount * 2 > mCoords.length) {
            mCoords = java.util.Arrays.copyOf(mCoords, mCoordCount + extraPtCount * 2);
        }
    }

    public void moveTo(float x, float y) {
        // Consecutive moves collapse into the last one, as in Skia.
        if (mVerbCount > 0 && mVerbs[mVerbCount - 1] == VERB_MOVE) {
            mCoords[mCoordCount - 2] = x;
            mCoords[mCoordCount - 1] = y;
            mLastX = x;
            mLastY = y;
        } else {
            addVerb(VERB_MOVE);
            addPoint(x, y);
        }
        mMoveX = x;
        mMoveY = y;
        mHasCurrent = true;
    }

    public void rMoveTo(float dx, float dy) {
        moveTo(mLastX + dx, mLastY + dy);
    }

    private void afterClose() {
        // Drawing after close() continues from the contour's start.
        if (mVerbCount > 0 && mVerbs[mVerbCount - 1] == VERB_CLOSE) {
            moveTo(mMoveX, mMoveY);
        }
    }

    public void lineTo(float x, float y) {
        ensureStart(0, 0);
        afterClose();
        addVerb(VERB_LINE);
        addPoint(x, y);
    }

    public void rLineTo(float dx, float dy) {
        lineTo(mLastX + dx, mLastY + dy);
    }

    public void quadTo(float x1, float y1, float x2, float y2) {
        ensureStart(0, 0);
        afterClose();
        addVerb(VERB_QUAD);
        addPoint(x1, y1);
        addPoint(x2, y2);
    }

    public void rQuadTo(float dx1, float dy1, float dx2, float dy2) {
        quadTo(mLastX + dx1, mLastY + dy1, mLastX + dx2, mLastY + dy2);
    }

    public void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        ensureStart(0, 0);
        afterClose();
        addVerb(VERB_CUBIC);
        addPoint(x1, y1);
        addPoint(x2, y2);
        addPoint(x3, y3);
    }

    public void rCubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        cubicTo(mLastX + x1, mLastY + y1, mLastX + x2, mLastY + y2, mLastX + x3, mLastY + y3);
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle, boolean forceMoveTo) {
        if (oval == null) {
            return;
        }
        arcTo(oval.left, oval.top, oval.right, oval.bottom, startAngle, sweepAngle, forceMoveTo);
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle) {
        arcTo(oval, startAngle, sweepAngle, false);
    }

    /**
     * Appends an arc of the oval. Angles are degrees clockwise from the positive x axis. The
     * arc is joined to the current point with a line unless forceMoveTo starts a new contour.
     */
    public void arcTo(float left, float top, float right, float bottom,
                      float startAngle, float sweepAngle, boolean forceMoveTo) {
        final float rx = (right - left) / 2f;
        final float ry = (bottom - top) / 2f;
        final float cx = left + rx;
        final float cy = top + ry;
        final double start = Math.toRadians(startAngle);
        final float sx = (float) (cx + rx * Math.cos(start));
        final float sy = (float) (cy + ry * Math.sin(start));
        if (forceMoveTo || !mHasCurrent) {
            moveTo(sx, sy);
        } else {
            lineTo(sx, sy);
        }
        float sweep = sweepAngle;
        if (sweep > 360f) {
            sweep = 360f;
        } else if (sweep < -360f) {
            sweep = -360f;
        }
        if (sweep == 0f || (rx == 0f && ry == 0f)) {
            return;
        }
        final int segments = (int) Math.ceil(Math.abs(sweep) / 90f - 1e-6);
        final double step = Math.toRadians(sweep) / segments;
        final double t = 4.0 / 3.0 * Math.tan(step / 4.0);
        double a = start;
        for (int i = 0; i < segments; i++) {
            final double b = a + step;
            final double ca = Math.cos(a), sa = Math.sin(a);
            final double cb = Math.cos(b), sb = Math.sin(b);
            cubicTo((float) (cx + rx * (ca - t * sa)), (float) (cy + ry * (sa + t * ca)),
                    (float) (cx + rx * (cb + t * sb)), (float) (cy + ry * (sb - t * cb)),
                    (float) (cx + rx * cb), (float) (cy + ry * sb));
            a = b;
        }
    }

    public void close() {
        if (mVerbCount > 0 && mVerbs[mVerbCount - 1] != VERB_CLOSE) {
            addVerb(VERB_CLOSE);
            mLastX = mMoveX;
            mLastY = mMoveY;
        }
    }

    public void addRect(RectF rect, Direction dir) {
        if (rect != null) {
            addRect(rect.left, rect.top, rect.right, rect.bottom, dir);
        }
    }

    public void addRect(float left, float top, float right, float bottom, Direction dir) {
        moveTo(left, top);
        if (dir == Direction.CCW) {
            lineTo(left, bottom);
            lineTo(right, bottom);
            lineTo(right, top);
        } else {
            lineTo(right, top);
            lineTo(right, bottom);
            lineTo(left, bottom);
        }
        close();
    }

    public void addOval(RectF oval, Direction dir) {
        if (oval != null) {
            addOval(oval.left, oval.top, oval.right, oval.bottom, dir);
        }
    }

    /** A full oval starting at its right-most point. */
    public void addOval(float left, float top, float right, float bottom, Direction dir) {
        final float cx = (left + right) / 2f;
        final float cy = (top + bottom) / 2f;
        final float rx = (right - left) / 2f;
        final float ry = (bottom - top) / 2f;
        final float kx = rx * KAPPA;
        final float ky = ry * KAPPA;
        moveTo(right, cy);
        if (dir == Direction.CCW) {
            cubicTo(right, cy - ky, cx + kx, top, cx, top);
            cubicTo(cx - kx, top, left, cy - ky, left, cy);
            cubicTo(left, cy + ky, cx - kx, bottom, cx, bottom);
            cubicTo(cx + kx, bottom, right, cy + ky, right, cy);
        } else {
            cubicTo(right, cy + ky, cx + kx, bottom, cx, bottom);
            cubicTo(cx - kx, bottom, left, cy + ky, left, cy);
            cubicTo(left, cy - ky, cx - kx, top, cx, top);
            cubicTo(cx + kx, top, right, cy - ky, right, cy);
        }
        close();
    }

    public void addCircle(float x, float y, float radius, Direction dir) {
        if (radius > 0) {
            addOval(x - radius, y - radius, x + radius, y + radius, dir);
        }
    }

    public void addArc(RectF oval, float startAngle, float sweepAngle) {
        arcTo(oval, startAngle, sweepAngle, true);
    }

    public void addArc(float left, float top, float right, float bottom,
                       float startAngle, float sweepAngle) {
        arcTo(left, top, right, bottom, startAngle, sweepAngle, true);
    }

    public void addRoundRect(RectF rect, float rx, float ry, Direction dir) {
        if (rect != null) {
            addRoundRect(rect.left, rect.top, rect.right, rect.bottom, rx, ry, dir);
        }
    }

    public void addRoundRect(float left, float top, float right, float bottom,
                             float rx, float ry, Direction dir) {
        addRoundRect(left, top, right, bottom,
                new float[] {rx, ry, rx, ry, rx, ry, rx, ry}, dir);
    }

    public void addRoundRect(RectF rect, float[] radii, Direction dir) {
        if (rect != null) {
            addRoundRect(rect.left, rect.top, rect.right, rect.bottom, radii, dir);
        }
    }

    /** Radii are x/y pairs for the top-left, top-right, bottom-right and bottom-left corners. */
    public void addRoundRect(float left, float top, float right, float bottom,
                             float[] radii, Direction dir) {
        if (radii == null || radii.length < 8) {
            throw new ArrayIndexOutOfBoundsException("radii[] needs 8 values");
        }
        float[] r = radii.clone();
        final float w = right - left;
        final float h = bottom - top;
        // Radii that do not fit are scaled down together, preserving their ratios.
        float scale = 1f;
        scale = fit(scale, r[0] + r[2], w);
        scale = fit(scale, r[6] + r[4], w);
        scale = fit(scale, r[1] + r[7], h);
        scale = fit(scale, r[3] + r[5], h);
        if (scale < 1f) {
            for (int i = 0; i < 8; i++) {
                r[i] *= scale;
            }
        }
        if (dir == Direction.CCW) {
            moveTo(left + r[0], top);
            roundCorner(left, top, r[0], r[1], 270f, -90f, left + r[0], top + r[1]);
            lineTo(left, bottom - r[7]);
            roundCorner(left, bottom, r[6], r[7], 180f, -90f, left + r[6], bottom - r[7]);
            lineTo(right - r[4], bottom);
            roundCorner(right, bottom, r[4], r[5], 90f, -90f, right - r[4], bottom - r[5]);
            lineTo(right, top + r[3]);
            roundCorner(right, top, r[2], r[3], 0f, -90f, right - r[2], top + r[3]);
            close();
            return;
        }
        moveTo(left + r[0], top);
        lineTo(right - r[2], top);
        roundCorner(right, top, r[2], r[3], 270f, 90f, right - r[2], top + r[3]);
        lineTo(right, bottom - r[5]);
        roundCorner(right, bottom, r[4], r[5], 0f, 90f, right - r[4], bottom - r[5]);
        lineTo(left + r[6], bottom);
        roundCorner(left, bottom, r[6], r[7], 90f, 90f, left + r[6], bottom - r[7]);
        lineTo(left, top + r[1]);
        roundCorner(left, top, r[0], r[1], 180f, 90f, left + r[0], top + r[1]);
        close();
    }

    private static float fit(float scale, float sum, float limit) {
        return sum > limit && sum > 0 ? Math.min(scale, limit / sum) : scale;
    }

    /** One rounded corner as an arc centred on (cx, cy); zero radii leave a sharp corner. */
    private void roundCorner(float cornerX, float cornerY, float rx, float ry, float start,
            float sweep, float cx, float cy) {
        if (rx <= 0f || ry <= 0f) {
            lineTo(cornerX, cornerY);
            return;
        }
        arcTo(cx - rx, cy - ry, cx + rx, cy + ry, start, sweep, false);
    }

    public void addPath(Path src, float dx, float dy) {
        if (src == null || src.isEmpty()) {
            return;
        }
        int ci = 0;
        for (int v = 0; v < src.mVerbCount; v++) {
            final byte verb = src.mVerbs[v];
            switch (verb) {
                case VERB_MOVE:
                    moveTo(src.mCoords[ci] + dx, src.mCoords[ci + 1] + dy);
                    break;
                case VERB_LINE:
                    lineTo(src.mCoords[ci] + dx, src.mCoords[ci + 1] + dy);
                    break;
                case VERB_QUAD:
                    quadTo(src.mCoords[ci] + dx, src.mCoords[ci + 1] + dy,
                            src.mCoords[ci + 2] + dx, src.mCoords[ci + 3] + dy);
                    break;
                case VERB_CUBIC:
                    cubicTo(src.mCoords[ci] + dx, src.mCoords[ci + 1] + dy,
                            src.mCoords[ci + 2] + dx, src.mCoords[ci + 3] + dy,
                            src.mCoords[ci + 4] + dx, src.mCoords[ci + 5] + dy);
                    break;
                default:
                    close();
                    break;
            }
            ci += pointsFor(verb) * 2;
        }
    }

    public void addPath(Path src) {
        addPath(src, 0, 0);
    }

    public void addPath(Path src, Matrix matrix) {
        if (src == null || src.isEmpty()) {
            return;
        }
        Path temp = new Path();
        src.transform(matrix, temp);
        addPath(temp, 0, 0);
    }

    public void offset(float dx, float dy, Path dst) {
        if (dst != null) {
            dst.set(this);
            dst.offset(dx, dy);
        } else {
            offset(dx, dy);
        }
    }

    public void offset(float dx, float dy) {
        for (int i = 0; i < mCoordCount; i += 2) {
            mCoords[i] += dx;
            mCoords[i + 1] += dy;
        }
        mLastX += dx;
        mLastY += dy;
        mMoveX += dx;
        mMoveY += dy;
    }

    public void setLastPoint(float dx, float dy) {
        if (mCoordCount >= 2) {
            mCoords[mCoordCount - 2] = dx;
            mCoords[mCoordCount - 1] = dy;
        } else {
            moveTo(dx, dy);
        }
        mLastX = dx;
        mLastY = dy;
    }

    public void transform(Matrix matrix, Path dst) {
        if (dst != null) {
            dst.set(this);
            dst.transform(matrix);
        } else {
            transform(matrix);
        }
    }

    public void transform(Matrix matrix) {
        if (matrix == null || mCoordCount == 0) {
            return;
        }
        final float[] pts = java.util.Arrays.copyOf(mCoords, mCoordCount);
        matrix.mapPoints(pts);
        System.arraycopy(pts, 0, mCoords, 0, mCoordCount);
        final float[] last = {mLastX, mLastY, mMoveX, mMoveY};
        matrix.mapPoints(last);
        mLastX = last[0];
        mLastY = last[1];
        mMoveX = last[2];
        mMoveY = last[3];
    }

    /** Boolean path operations are not computed: the second path is simply appended. */
    public boolean op(Path path, Op op) {
        return op(this, path, op);
    }

    public boolean op(Path path1, Path path2, Op op) {
        if (path1 != null && path2 != null) {
            if (path1 != this) {
                set(path1);
            }
            addPath(path2);
            return true;
        }
        return false;
    }

    /**
     * Approximates the path with line segments. Returns triples of (fraction of total length,
     * x, y); a moveTo repeats the previous fraction.
     */
    public float[] approximate(float acceptableError) {
        if (acceptableError < 0) {
            throw new IllegalArgumentException("AcceptableError must be greater than or equal to 0");
        }
        final float[] flat = flatten(acceptableError, null, null);
        if (flat.length == 0) {
            return new float[] {0f, 0f, 0f, 1f, 0f, 0f};
        }
        // flat holds (cumulative length, x, y); turn the length into a fraction.
        final float total = flat[flat.length - 3];
        final float[] out = flat.clone();
        for (int i = 0; i < out.length; i += 3) {
            out[i] = total > 0f ? flat[i] / total : (i == out.length - 3 ? 1f : 0f);
        }
        if (out.length == 3) {
            // A lone point still needs a start and an end.
            return new float[] {0f, out[1], out[2], 1f, out[1], out[2]};
        }
        return out;
    }

    /**
     * Flattens the path into (cumulative length, x, y) triples. When contourStarts is non-null
     * it receives the triple index at which each contour begins; contourClosed, when non-null,
     * receives whether each contour ended with close().
     */
    float[] flatten(float tolerance, java.util.ArrayList<Integer> contourStarts,
            java.util.ArrayList<Boolean> contourClosed) {
        float[] out = new float[48];
        int n = 0;
        float length = 0f;
        float x = 0, y = 0, startX = 0, startY = 0;
        boolean open = false;
        int ci = 0;
        for (int v = 0; v < mVerbCount; v++) {
            final byte verb = mVerbs[v];
            switch (verb) {
                case VERB_MOVE:
                    x = startX = mCoords[ci];
                    y = startY = mCoords[ci + 1];
                    if (contourStarts != null) {
                        contourStarts.add(n / 3);
                    }
                    if (contourClosed != null) {
                        contourClosed.add(Boolean.FALSE);
                    }
                    out = push(out, n, length, x, y);
                    n += 3;
                    open = true;
                    break;
                case VERB_LINE: {
                    final float nx = mCoords[ci], ny = mCoords[ci + 1];
                    length += (float) Math.hypot(nx - x, ny - y);
                    out = push(out, n, length, nx, ny);
                    n += 3;
                    x = nx;
                    y = ny;
                    break;
                }
                case VERB_QUAD: {
                    final float x1 = mCoords[ci], y1 = mCoords[ci + 1];
                    final float x2 = mCoords[ci + 2], y2 = mCoords[ci + 3];
                    final float dev = (float) Math.hypot(x - 2 * x1 + x2, y - 2 * y1 + y2);
                    final int steps = segmentCount(dev * 0.25f, tolerance);
                    for (int i = 1; i <= steps; i++) {
                        final float t = (float) i / steps;
                        final float u = 1 - t;
                        final float px = u * u * x + 2 * u * t * x1 + t * t * x2;
                        final float py = u * u * y + 2 * u * t * y1 + t * t * y2;
                        length += (float) Math.hypot(px - out[n - 2], py - out[n - 1]);
                        out = push(out, n, length, px, py);
                        n += 3;
                    }
                    x = x2;
                    y = y2;
                    break;
                }
                case VERB_CUBIC: {
                    final float x1 = mCoords[ci], y1 = mCoords[ci + 1];
                    final float x2 = mCoords[ci + 2], y2 = mCoords[ci + 3];
                    final float x3 = mCoords[ci + 4], y3 = mCoords[ci + 5];
                    final float d1 = (float) Math.hypot(x - 2 * x1 + x2, y - 2 * y1 + y2);
                    final float d2 = (float) Math.hypot(x1 - 2 * x2 + x3, y1 - 2 * y2 + y3);
                    final int steps = segmentCount(Math.max(d1, d2) * 0.75f, tolerance);
                    for (int i = 1; i <= steps; i++) {
                        final float t = (float) i / steps;
                        final float u = 1 - t;
                        final float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
                        final float px = a * x + b * x1 + c * x2 + d * x3;
                        final float py = a * y + b * y1 + c * y2 + d * y3;
                        length += (float) Math.hypot(px - out[n - 2], py - out[n - 1]);
                        out = push(out, n, length, px, py);
                        n += 3;
                    }
                    x = x3;
                    y = y3;
                    break;
                }
                default:
                    if (contourClosed != null && !contourClosed.isEmpty()) {
                        contourClosed.set(contourClosed.size() - 1, Boolean.TRUE);
                    }
                    if (open && (x != startX || y != startY)) {
                        length += (float) Math.hypot(startX - x, startY - y);
                        out = push(out, n, length, startX, startY);
                        n += 3;
                    }
                    x = startX;
                    y = startY;
                    break;
            }
            ci += pointsFor(verb) * 2;
        }
        return java.util.Arrays.copyOf(out, n);
    }

    private static int segmentCount(float deviation, float tolerance) {
        if (tolerance <= 0f) {
            return 100;
        }
        final int n = (int) Math.ceil(Math.sqrt(deviation / tolerance));
        return Math.max(1, Math.min(n, 1000));
    }

    private static float[] push(float[] out, int n, float length, float x, float y) {
        if (n + 3 > out.length) {
            out = java.util.Arrays.copyOf(out, out.length * 2);
        }
        out[n] = length;
        out[n + 1] = x;
        out[n + 2] = y;
        return out;
    }
}
