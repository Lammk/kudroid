package android.graphics;

/**
 * A color filter for transforming source pixels using PorterDuff blending modes.
 */
public class PorterDuffColorFilter extends ColorFilter {
    private int mColor;
    private PorterDuff.Mode mMode;

    public PorterDuffColorFilter() {
        this(0, PorterDuff.Mode.SRC_ATOP);
    }

    public PorterDuffColorFilter(int color, PorterDuff.Mode mode) {
        mColor = color;
        mMode = mode != null ? mode : PorterDuff.Mode.SRC_ATOP;
    }

    public int getColor() {
        return mColor;
    }

    public PorterDuff.Mode getMode() {
        return mMode;
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) return true;
        if (object == null || getClass() != object.getClass()) return false;
        PorterDuffColorFilter other = (PorterDuffColorFilter) object;
        return mColor == other.mColor && mMode == other.mMode;
    }

    @Override
    public int hashCode() {
        return 31 * mColor + (mMode != null ? mMode.hashCode() : 0);
    }
}
