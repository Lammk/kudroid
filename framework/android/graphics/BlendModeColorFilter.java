package android.graphics;

public final class BlendModeColorFilter extends ColorFilter {
    private final int mColor;
    private final BlendMode mMode;

    public BlendModeColorFilter(int color, BlendMode mode) {
        mColor = color;
        mMode = mode;
    }

    public int getColor() { return mColor; }
    public BlendMode getMode() { return mMode; }
}
