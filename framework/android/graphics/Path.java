package android.graphics;

import java.util.ArrayList;

/**
 * The Path class encapsulates compound (multiple contour) geometric paths
 * following the AOSP specification.
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

    private FillType mFillType = FillType.WINDING;
    private final ArrayList<float[]> mPoints = new ArrayList<float[]>();
    private float mLastX = 0;
    private float mLastY = 0;
    private boolean mIsEmpty = true;
    private final RectF mBounds = new RectF();

    public Path() {
        reset();
    }

    public Path(Path src) {
        set(src);
    }

    public void reset() {
        mFillType = FillType.WINDING;
        mPoints.clear();
        mLastX = 0;
        mLastY = 0;
        mIsEmpty = true;
        mBounds.setEmpty();
    }

    public void rewind() {
        mPoints.clear();
        mLastX = 0;
        mLastY = 0;
        mIsEmpty = true;
        mBounds.setEmpty();
    }

    public void set(Path src) {
        if (this == src) return;
        reset();
        if (src != null) {
            mFillType = src.mFillType;
            for (float[] pt : src.mPoints) {
                mPoints.add(pt.clone());
            }
            mLastX = src.mLastX;
            mLastY = src.mLastY;
            mIsEmpty = src.mIsEmpty;
            mBounds.set(src.mBounds);
        }
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
        if (mFillType == FillType.WINDING) mFillType = FillType.INVERSE_WINDING;
        else if (mFillType == FillType.EVEN_ODD) mFillType = FillType.INVERSE_EVEN_ODD;
        else if (mFillType == FillType.INVERSE_WINDING) mFillType = FillType.WINDING;
        else if (mFillType == FillType.INVERSE_EVEN_ODD) mFillType = FillType.EVEN_ODD;
    }

    public boolean isEmpty() {
        return mIsEmpty;
    }

    public boolean isRect(RectF rect) {
        if (mPoints.size() != 4 || mIsEmpty) return false;
        if (rect != null) rect.set(mBounds);
        return true;
    }

    public void computeBounds(RectF bounds, boolean exact) {
        if (bounds != null) {
            bounds.set(mBounds);
        }
    }

    public void incReserve(int extraPtCount) {
    }

    private void updateBounds(float x, float y) {
        if (mIsEmpty) {
            mBounds.set(x, y, x, y);
            mIsEmpty = false;
        } else {
            mBounds.union(x, y);
        }
    }

    public void moveTo(float x, float y) {
        mPoints.add(new float[] { x, y });
        mLastX = x;
        mLastY = y;
        updateBounds(x, y);
    }

    public void rMoveTo(float dx, float dy) {
        moveTo(mLastX + dx, mLastY + dy);
    }

    public void lineTo(float x, float y) {
        mPoints.add(new float[] { x, y });
        mLastX = x;
        mLastY = y;
        updateBounds(x, y);
    }

    public void rLineTo(float dx, float dy) {
        lineTo(mLastX + dx, mLastY + dy);
    }

    public void quadTo(float x1, float y1, float x2, float y2) {
        mPoints.add(new float[] { x1, y1 });
        mPoints.add(new float[] { x2, y2 });
        mLastX = x2;
        mLastY = y2;
        updateBounds(x1, y1);
        updateBounds(x2, y2);
    }

    public void rQuadTo(float dx1, float dy1, float dx2, float dy2) {
        quadTo(mLastX + dx1, mLastY + dy1, mLastX + dx2, mLastY + dy2);
    }

    public void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        mPoints.add(new float[] { x1, y1 });
        mPoints.add(new float[] { x2, y2 });
        mPoints.add(new float[] { x3, y3 });
        mLastX = x3;
        mLastY = y3;
        updateBounds(x1, y1);
        updateBounds(x2, y2);
        updateBounds(x3, y3);
    }

    public void rCubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        cubicTo(mLastX + x1, mLastY + y1, mLastX + x2, mLastY + y2, mLastX + x3, mLastY + y3);
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle, boolean forceMoveTo) {
        if (oval == null) return;
        arcTo(oval.left, oval.top, oval.right, oval.bottom, startAngle, sweepAngle, forceMoveTo);
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle) {
        arcTo(oval, startAngle, sweepAngle, false);
    }

    public void arcTo(float left, float top, float right, float bottom,
                      float startAngle, float sweepAngle, boolean forceMoveTo) {
        updateBounds(left, top);
        updateBounds(right, bottom);
        if (forceMoveTo) {
            moveTo(right, bottom);
        } else {
            lineTo(right, bottom);
        }
    }

    public void close() {
        if (!mPoints.isEmpty()) {
            float[] first = mPoints.get(0);
            mLastX = first[0];
            mLastY = first[1];
        }
    }

    public void addRect(RectF rect, Direction dir) {
        if (rect != null) {
            addRect(rect.left, rect.top, rect.right, rect.bottom, dir);
        }
    }

    public void addRect(float left, float top, float right, float bottom, Direction dir) {
        moveTo(left, top);
        lineTo(right, top);
        lineTo(right, bottom);
        lineTo(left, bottom);
        close();
    }

    public void addOval(RectF oval, Direction dir) {
        if (oval != null) {
            addOval(oval.left, oval.top, oval.right, oval.bottom, dir);
        }
    }

    public void addOval(float left, float top, float right, float bottom, Direction dir) {
        updateBounds(left, top);
        updateBounds(right, bottom);
        moveTo(right, (top + bottom) * 0.5f);
        lineTo(left, (top + bottom) * 0.5f);
        close();
    }

    public void addCircle(float x, float y, float radius, Direction dir) {
        addOval(x - radius, y - radius, x + radius, y + radius, dir);
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
        addRect(left, top, right, bottom, dir);
    }

    public void addRoundRect(RectF rect, float[] radii, Direction dir) {
        if (rect != null) {
            addRect(rect, dir);
        }
    }

    public void addRoundRect(float left, float top, float right, float bottom,
                             float[] radii, Direction dir) {
        addRect(left, top, right, bottom, dir);
    }

    public void addPath(Path src, float dx, float dy) {
        if (src == null || src.isEmpty()) return;
        for (float[] pt : src.mPoints) {
            mPoints.add(new float[] { pt[0] + dx, pt[1] + dy });
            updateBounds(pt[0] + dx, pt[1] + dy);
        }
        mLastX = src.mLastX + dx;
        mLastY = src.mLastY + dy;
    }

    public void addPath(Path src) {
        addPath(src, 0, 0);
    }

    public void addPath(Path src, Matrix matrix) {
        if (src == null || src.isEmpty()) return;
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
        for (float[] pt : mPoints) {
            pt[0] += dx;
            pt[1] += dy;
        }
        mLastX += dx;
        mLastY += dy;
        mBounds.offset(dx, dy);
    }

    public void setLastPoint(float dx, float dy) {
        if (!mPoints.isEmpty()) {
            float[] last = mPoints.get(mPoints.size() - 1);
            last[0] = dx;
            last[1] = dy;
        }
        mLastX = dx;
        mLastY = dy;
        updateBounds(dx, dy);
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
        if (matrix == null || mPoints.isEmpty()) return;
        float[] pts = new float[mPoints.size() * 2];
        for (int i = 0; i < mPoints.size(); i++) {
            pts[i * 2 + 0] = mPoints.get(i)[0];
            pts[i * 2 + 1] = mPoints.get(i)[1];
        }
        matrix.mapPoints(pts);
        mBounds.setEmpty();
        mIsEmpty = true;
        for (int i = 0; i < mPoints.size(); i++) {
            mPoints.get(i)[0] = pts[i * 2 + 0];
            mPoints.get(i)[1] = pts[i * 2 + 1];
            updateBounds(pts[i * 2 + 0], pts[i * 2 + 1]);
        }
    }

    public boolean op(Path path, Op op) {
        return op(this, path, op);
    }

    public boolean op(Path path1, Path path2, Op op) {
        if (path1 != null && path2 != null) {
            set(path1);
            addPath(path2);
            return true;
        }
        return false;
    }
}
