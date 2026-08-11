package android.graphics;

/**
 * Minimal android.graphics.Rect implementation.
 *
 * Represents a rectangle. For KuDroid's minimal framework, stores left/top/
 * right/bottom.
 */
public final class Rect {
    public int left;
    public int top;
    public int right;
    public int bottom;

    public Rect() {
    }

    public Rect(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public Rect(Rect r) {
        if (r != null) {
            left = r.left;
            top = r.top;
            right = r.right;
            bottom = r.bottom;
        }
    }

    public int width() {
        return right - left;
    }

    public int height() {
        return bottom - top;
    }

    public boolean isEmpty() {
        return left >= right || top >= bottom;
    }

    public void set(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public void set(Rect r) {
        if (r != null) {
            left = r.left;
            top = r.top;
            right = r.right;
            bottom = r.bottom;
        }
    }

    public void offset(int dx, int dy) {
        left += dx;
        top += dy;
        right += dx;
        bottom += dy;
    }

    public boolean contains(int x, int y) {
        return left < right && top < bottom && x >= left && x < right && y >= top && y < bottom;
    }

    public boolean intersect(Rect r) {
        if (r == null) return false;
        int maxLeft = Math.max(left, r.left);
        int minRight = Math.min(right, r.right);
        int maxTop = Math.max(top, r.top);
        int minBottom = Math.min(bottom, r.bottom);
        if (maxLeft < minRight && maxTop < minBottom) {
            left = maxLeft;
            top = maxTop;
            right = minRight;
            bottom = minBottom;
            return true;
        }
        return false;
    }

    @Override
    public String toString() {
        return "Rect(" + left + ", " + top + " - " + right + ", " + bottom + ")";
    }
}
