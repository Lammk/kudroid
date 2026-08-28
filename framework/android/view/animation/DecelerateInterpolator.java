package android.view.animation;

public class DecelerateInterpolator implements Interpolator {
    private final float mFactor;
    public DecelerateInterpolator() { mFactor = 1.0f; }
    public DecelerateInterpolator(float factor) { mFactor = factor; }
    public float getInterpolation(float input) {
        return mFactor == 1.0f ? 1.0f - (1.0f - input) * (1.0f - input) : (float)(1.0f - Math.pow((1.0f - input), 2 * mFactor));
    }
}
