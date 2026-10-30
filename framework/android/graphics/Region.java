package android.graphics;

/**
 * A region reduced to its bounding rectangle. Combining operations act on the bounds, so
 * non-rectangular results are approximated by the smallest rectangle that holds them.
 */
public class Region {
    public enum Op {
        DIFFERENCE(0), INTERSECT(1), UNION(2), XOR(3), REVERSE_DIFFERENCE(4), REPLACE(5);

        public final int nativeInt;

        Op(int nativeInt) {
            this.nativeInt = nativeInt;
        }
    }

    private final Rect mBounds = new Rect();

    public Region() {}

    public Region(Region region) {
        if (region != null) mBounds.set(region.mBounds);
    }

    public Region(Rect r) {
        if (r != null) mBounds.set(r);
    }

    public Region(int left, int top, int right, int bottom) {
        mBounds.set(left, top, right, bottom);
    }

    public void setEmpty() { mBounds.setEmpty(); }

    public boolean set(Region region) {
        mBounds.set(region.mBounds);
        return !mBounds.isEmpty();
    }

    public boolean set(Rect r) {
        mBounds.set(r);
        return !mBounds.isEmpty();
    }

    public boolean set(int left, int top, int right, int bottom) {
        mBounds.set(left, top, right, bottom);
        return !mBounds.isEmpty();
    }

    public boolean setPath(Path path, Region clip) {
        final RectF pathBounds = new RectF();
        path.computeBounds(pathBounds, true);
        final Rect r = new Rect();
        pathBounds.roundOut(r);
        if (clip != null && !r.intersect(clip.mBounds)) r.setEmpty();
        mBounds.set(r);
        return !mBounds.isEmpty();
    }

    public boolean isEmpty() { return mBounds.isEmpty(); }
    public boolean isRect() { return !mBounds.isEmpty(); }
    public boolean isComplex() { return false; }

    public Rect getBounds() { return new Rect(mBounds); }

    public boolean getBounds(Rect r) {
        r.set(mBounds);
        return !mBounds.isEmpty();
    }

    public Path getBoundaryPath() {
        final Path p = new Path();
        if (!mBounds.isEmpty()) {
            p.addRect(mBounds.left, mBounds.top, mBounds.right, mBounds.bottom, Path.Direction.CW);
        }
        return p;
    }

    public boolean getBoundaryPath(Path path) {
        path.set(getBoundaryPath());
        return !mBounds.isEmpty();
    }

    public boolean contains(int x, int y) { return mBounds.contains(x, y); }

    public boolean quickContains(Rect r) { return mBounds.contains(r); }

    public boolean quickContains(int left, int top, int right, int bottom) {
        return mBounds.contains(left, top, right, bottom);
    }

    public boolean quickReject(Rect r) {
        return mBounds.isEmpty() || r.isEmpty() || !Rect.intersects(mBounds, r);
    }

    public boolean quickReject(int left, int top, int right, int bottom) {
        return mBounds.isEmpty() || left >= right || top >= bottom
                || !Rect.intersects(mBounds, new Rect(left, top, right, bottom));
    }

    public boolean quickReject(Region rgn) {
        return mBounds.isEmpty() || rgn.mBounds.isEmpty() || !Rect.intersects(mBounds, rgn.mBounds);
    }

    public void translate(int dx, int dy) { mBounds.offset(dx, dy); }

    public final boolean union(Rect r) { return op(r, Op.UNION); }

    public boolean op(Rect r, Op op) { return combine(r, op); }

    public boolean op(int left, int top, int right, int bottom, Op op) {
        return combine(new Rect(left, top, right, bottom), op);
    }

    public boolean op(Region region, Op op) { return combine(region.mBounds, op); }

    public boolean op(Rect rect, Region region, Op op) {
        mBounds.set(rect);
        return combine(region.mBounds, op);
    }

    public boolean op(Region region1, Region region2, Op op) {
        mBounds.set(region1.mBounds);
        return combine(region2.mBounds, op);
    }

    private boolean combine(Rect other, Op op) {
        switch (op) {
            case INTERSECT:
                if (!mBounds.intersect(other)) mBounds.setEmpty();
                break;
            case UNION:
            case XOR:
                if (mBounds.isEmpty()) mBounds.set(other);
                else if (!other.isEmpty()) mBounds.union(other);
                break;
            case REPLACE:
                mBounds.set(other);
                break;
            case REVERSE_DIFFERENCE:
                mBounds.set(other);
                break;
            case DIFFERENCE:
            default:
                break;
        }
        return !mBounds.isEmpty();
    }
}
