package android.animation;

/**
 * android.animation.TimeInterpolator — map linear progress [0,1] to nonlinear.
 */
public interface TimeInterpolator {
    float getInterpolation(float input);
}
