package android.animation;

/**
 * android.animation.TimeInterpolator — map tiến độ tuyến tính [0,1] sang phi tuyến.
 */
public interface TimeInterpolator {
    float getInterpolation(float input);
}
