package android.view.animation;

public class AccelerateInterpolator implements Interpolator {
    private final float mFactor;
    public AccelerateInterpolator() { mFactor = 1.0f; }
    public AccelerateInterpolator(float factor) { mFactor = factor; }
    public float getInterpolation(float input) {
        return mFactor == 1.0f ? input * input : (float)Math.pow(input, 2 * mFactor);
    }
}
