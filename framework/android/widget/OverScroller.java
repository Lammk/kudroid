package android.widget;

import android.content.Context;
import android.view.ViewConfiguration;
import android.view.animation.AnimationUtils;
import android.view.animation.Interpolator;

/**
 * Scrolling and flinging over time. Positions are computed on demand from the elapsed time, so
 * a caller drives it by polling {@link #computeScrollOffset()} each frame.
 *
 * A fling follows Android's deceleration model (the same duration and distance formulas) with a
 * power-law position curve; flings stop at their bounds instead of overshooting.
 */
public class OverScroller {
    private static final int SCROLL_MODE = 0;
    private static final int FLING_MODE = 1;

    private static final float INFLEXION = 0.35f;
    private static final float DECELERATION_RATE = (float) (Math.log(0.78) / Math.log(0.9));

    private final Axis mX = new Axis();
    private final Axis mY = new Axis();
    private Interpolator mInterpolator;
    private final float mPhysicalCoeff;
    private float mFriction = ViewConfiguration.getScrollFriction();
    private int mMode = SCROLL_MODE;

    private final class Axis {
        int start;
        int current;
        int finalPos;
        float velocity;
        long startTime;
        int duration;
        boolean finished = true;
        boolean fling;
        int distance;

        void finish() {
            current = finalPos;
            finished = true;
        }
    }

    public OverScroller(Context context) {
        this(context, null);
    }

    public OverScroller(Context context, Interpolator interpolator) {
        mInterpolator = interpolator != null ? interpolator : new ViscousFluidInterpolator();
        final float density = context != null && context.getResources() != null
                ? context.getResources().getDisplayMetrics().density : 1f;
        mPhysicalCoeff = 9.80665f * 39.37f * (160f * density) * 0.84f;
    }

    public OverScroller(Context context, Interpolator interpolator, float bounceCoefficientX,
            float bounceCoefficientY) {
        this(context, interpolator);
    }

    public OverScroller(Context context, Interpolator interpolator, float bounceCoefficientX,
            float bounceCoefficientY, boolean flywheel) {
        this(context, interpolator);
    }

    public final void setFriction(float friction) {
        mFriction = friction;
    }

    public final boolean isFinished() {
        return mX.finished && mY.finished;
    }

    public final void forceFinished(boolean finished) {
        mX.finished = finished;
        mY.finished = finished;
    }

    public final int getCurrX() {
        return mX.current;
    }

    public final int getCurrY() {
        return mY.current;
    }

    public final int getStartX() {
        return mX.start;
    }

    public final int getStartY() {
        return mY.start;
    }

    public final int getFinalX() {
        return mX.finalPos;
    }

    public final int getFinalY() {
        return mY.finalPos;
    }

    public final int getDuration() {
        return Math.max(mX.duration, mY.duration);
    }

    public void setFinalX(int newX) {
        mX.finalPos = newX;
        mX.finished = false;
    }

    public void setFinalY(int newY) {
        mY.finalPos = newY;
        mY.finished = false;
    }

    public int timePassed() {
        return (int) (AnimationUtils.currentAnimationTimeMillis() - Math.min(mX.startTime, mY.startTime));
    }

    public void extendDuration(int extend) {
        mX.duration = mX.duration + extend;
        mY.duration = mY.duration + extend;
    }

    public float getCurrVelocity() {
        return (float) Math.hypot(velocityNow(mX), velocityNow(mY));
    }

    public boolean isOverScrolled() {
        return false;
    }

    public void setInterpolator(Interpolator interpolator) {
        mInterpolator = interpolator != null ? interpolator : new ViscousFluidInterpolator();
    }

    public boolean springBack(int startX, int startY, int minX, int maxX, int minY, int maxY) {
        mMode = FLING_MODE;
        final boolean x = springBackAxis(mX, startX, minX, maxX);
        final boolean y = springBackAxis(mY, startY, minY, maxY);
        return x || y;
    }

    private boolean springBackAxis(Axis a, int start, int min, int max) {
        a.start = start;
        a.current = start;
        a.finalPos = start;
        a.finished = true;
        a.duration = 0;
        if (start < min) {
            startAxis(a, start, min - start, 200, false);
            return true;
        }
        if (start > max) {
            startAxis(a, start, max - start, 200, false);
            return true;
        }
        return false;
    }

    public void startScroll(int startX, int startY, int dx, int dy) {
        startScroll(startX, startY, dx, dy, 250);
    }

    public void startScroll(int startX, int startY, int dx, int dy, int duration) {
        mMode = SCROLL_MODE;
        startAxis(mX, startX, dx, duration, false);
        startAxis(mY, startY, dy, duration, false);
    }

    private void startAxis(Axis a, int start, int delta, int duration, boolean fling) {
        a.start = start;
        a.current = start;
        a.finalPos = start + delta;
        a.distance = delta;
        a.duration = duration;
        a.startTime = AnimationUtils.currentAnimationTimeMillis();
        a.fling = fling;
        a.finished = false;
    }

    public void fling(int startX, int startY, int velocityX, int velocityY, int minX, int maxX,
            int minY, int maxY) {
        fling(startX, startY, velocityX, velocityY, minX, maxX, minY, maxY, 0, 0);
    }

    public void fling(int startX, int startY, int velocityX, int velocityY, int minX, int maxX,
            int minY, int maxY, int overX, int overY) {
        mMode = FLING_MODE;
        flingAxis(mX, startX, velocityX, minX, maxX);
        flingAxis(mY, startY, velocityY, minY, maxY);
    }

    private void flingAxis(Axis a, int start, int velocity, int min, int max) {
        a.start = start;
        a.current = start;
        a.velocity = velocity;
        a.fling = true;
        a.startTime = AnimationUtils.currentAnimationTimeMillis();
        if (start > max || start < min || velocity == 0) {
            a.finalPos = start;
            a.distance = 0;
            a.duration = 0;
            a.finished = true;
            return;
        }
        final double l = Math.log(INFLEXION * Math.abs(velocity) / (mFriction * mPhysicalCoeff));
        final double decelMinusOne = DECELERATION_RATE - 1.0;
        int duration = (int) (1000.0 * Math.exp(l / decelMinusOne));
        final double total = mFriction * mPhysicalCoeff * Math.exp(DECELERATION_RATE / decelMinusOne * l);
        int distance = (int) Math.round(total * Math.signum(velocity));
        int target = start + distance;
        if (target < min || target > max) {
            // Stop at the bound; the shorter trip takes proportionally less time.
            final int clamped = Math.max(min, Math.min(max, target));
            final double x = distance != 0 ? Math.abs((double) (clamped - start) / distance) : 0.0;
            duration = (int) (duration * (1.0 - Math.pow(1.0 - x, 1.0 / DECELERATION_RATE)));
            target = clamped;
            distance = clamped - start;
        }
        a.finalPos = target;
        a.distance = distance;
        a.duration = Math.max(1, duration);
        a.finished = distance == 0;
    }

    public void notifyHorizontalEdgeReached(int startX, int finalX, int overX) {
        mX.finish();
    }

    public void notifyVerticalEdgeReached(int startY, int finalY, int overY) {
        mY.finish();
    }

    public boolean computeScrollOffset() {
        if (isFinished()) return false;
        final long now = AnimationUtils.currentAnimationTimeMillis();
        update(mX, now);
        update(mY, now);
        return true;
    }

    private void update(Axis a, long now) {
        if (a.finished) return;
        final long elapsed = now - a.startTime;
        if (elapsed >= a.duration) {
            a.finish();
            return;
        }
        final float t = elapsed / (float) a.duration;
        final float p = a.fling
                ? 1f - (float) Math.pow(1f - t, DECELERATION_RATE)
                : mInterpolator.getInterpolation(t);
        a.current = a.start + Math.round(a.distance * p);
    }

    private float velocityNow(Axis a) {
        if (a.finished || a.duration <= 0) return 0f;
        final long elapsed = AnimationUtils.currentAnimationTimeMillis() - a.startTime;
        final float t = Math.max(0f, Math.min(1f, elapsed / (float) a.duration));
        final float slope = a.fling
                ? DECELERATION_RATE * (float) Math.pow(1f - t, DECELERATION_RATE - 1f)
                : 1f;
        return Math.abs(a.distance) * slope * 1000f / a.duration;
    }

    public void abortAnimation() {
        mX.finish();
        mY.finish();
    }

    /** Android's default easing for programmatic scrolls. */
    static final class ViscousFluidInterpolator implements Interpolator {
        private static final float SCALE = 1.0f / viscous(1.0f);
        private static final float NORMALIZE_OFFSET = 1.0f - SCALE * viscous(1.0f);

        private static float viscous(float x) {
            x *= 8.0f;
            if (x < 1.0f) {
                x -= (1.0f - (float) Math.exp(-x));
            } else {
                final float start = 0.36787944117f;
                x = 1.0f - (float) Math.exp(1.0f - x);
                x = start + x * (1.0f - start);
            }
            return x;
        }

        @Override
        public float getInterpolation(float input) {
            final float interpolated = SCALE * viscous(input);
            return interpolated > 0 ? interpolated + NORMALIZE_OFFSET : interpolated;
        }
    }
}
