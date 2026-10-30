package android.view;

import android.animation.Animator;
import android.animation.ValueAnimator;

public final class ViewAnimationUtils {
    private ViewAnimationUtils() {}

    /** A reveal is shown instantly: the returned animator runs, but nothing is clipped. */
    public static Animator createCircularReveal(View view, int centerX, int centerY,
            float startRadius, float endRadius) {
        return ValueAnimator.ofFloat(startRadius, endRadius);
    }
}
