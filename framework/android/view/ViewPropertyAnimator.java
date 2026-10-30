package android.view;

import android.animation.Animator;
import android.animation.TimeInterpolator;
import android.animation.ValueAnimator;

import java.util.ArrayList;

/**
 * Animates View properties with one ValueAnimator. Property calls made in the same
 * message are batched and the animation starts after them, as the platform does.
 */
public class ViewPropertyAnimator {
    private static final int ALPHA = 0, TRANSLATION_X = 1, TRANSLATION_Y = 2, TRANSLATION_Z = 3,
            X = 4, Y = 5, Z = 6, ROTATION = 7, ROTATION_X = 8, ROTATION_Y = 9, SCALE_X = 10,
            SCALE_Y = 11;

    private static final class Step {
        final int kind;
        final float value;
        final boolean relative;
        Step(int kind, float value, boolean relative) {
            this.kind = kind;
            this.value = value;
            this.relative = relative;
        }
    }

    private final View mView;
    private long mDuration = 300;
    private long mStartDelay = 0;
    private boolean mDurationSet;
    private TimeInterpolator mInterpolator;
    private Animator.AnimatorListener mListener;
    private ValueAnimator.AnimatorUpdateListener mUpdateListener;
    private Runnable mStartAction;
    private Runnable mEndAction;
    private ArrayList<Step> mPending = new ArrayList<>();
    private ValueAnimator mRunning;
    private boolean mStartScheduled;

    private final Runnable mStarter = new Runnable() {
        @Override
        public void run() {
            mStartScheduled = false;
            startNow();
        }
    };

    ViewPropertyAnimator(View view) {
        mView = view;
    }

    public ViewPropertyAnimator setDuration(long duration) {
        if (duration < 0) throw new IllegalArgumentException("Animators cannot have negative duration: " + duration);
        mDuration = duration;
        mDurationSet = true;
        return this;
    }

    public long getDuration() { return mDuration; }
    public long getStartDelay() { return mStartDelay; }

    public ViewPropertyAnimator setStartDelay(long startDelay) {
        if (startDelay < 0) throw new IllegalArgumentException("Animators cannot have negative start delay: " + startDelay);
        mStartDelay = startDelay;
        return this;
    }

    public ViewPropertyAnimator setInterpolator(TimeInterpolator interpolator) {
        mInterpolator = interpolator;
        return this;
    }

    public TimeInterpolator getInterpolator() { return mInterpolator; }

    public ViewPropertyAnimator setListener(Animator.AnimatorListener listener) {
        mListener = listener;
        return this;
    }

    public ViewPropertyAnimator setUpdateListener(ValueAnimator.AnimatorUpdateListener listener) {
        mUpdateListener = listener;
        return this;
    }

    public ViewPropertyAnimator withStartAction(Runnable runnable) {
        mStartAction = runnable;
        return this;
    }

    public ViewPropertyAnimator withEndAction(Runnable runnable) {
        mEndAction = runnable;
        return this;
    }

    public ViewPropertyAnimator withLayer() { return this; }

    public ViewPropertyAnimator alpha(float value) { return add(ALPHA, value, false); }
    public ViewPropertyAnimator alphaBy(float value) { return add(ALPHA, value, true); }
    public ViewPropertyAnimator translationX(float value) { return add(TRANSLATION_X, value, false); }
    public ViewPropertyAnimator translationXBy(float value) { return add(TRANSLATION_X, value, true); }
    public ViewPropertyAnimator translationY(float value) { return add(TRANSLATION_Y, value, false); }
    public ViewPropertyAnimator translationYBy(float value) { return add(TRANSLATION_Y, value, true); }
    public ViewPropertyAnimator translationZ(float value) { return add(TRANSLATION_Z, value, false); }
    public ViewPropertyAnimator translationZBy(float value) { return add(TRANSLATION_Z, value, true); }
    public ViewPropertyAnimator x(float value) { return add(X, value, false); }
    public ViewPropertyAnimator xBy(float value) { return add(X, value, true); }
    public ViewPropertyAnimator y(float value) { return add(Y, value, false); }
    public ViewPropertyAnimator yBy(float value) { return add(Y, value, true); }
    public ViewPropertyAnimator z(float value) { return add(Z, value, false); }
    public ViewPropertyAnimator zBy(float value) { return add(Z, value, true); }
    public ViewPropertyAnimator rotation(float value) { return add(ROTATION, value, false); }
    public ViewPropertyAnimator rotationBy(float value) { return add(ROTATION, value, true); }
    public ViewPropertyAnimator rotationX(float value) { return add(ROTATION_X, value, false); }
    public ViewPropertyAnimator rotationXBy(float value) { return add(ROTATION_X, value, true); }
    public ViewPropertyAnimator rotationY(float value) { return add(ROTATION_Y, value, false); }
    public ViewPropertyAnimator rotationYBy(float value) { return add(ROTATION_Y, value, true); }
    public ViewPropertyAnimator scaleX(float value) { return add(SCALE_X, value, false); }
    public ViewPropertyAnimator scaleXBy(float value) { return add(SCALE_X, value, true); }
    public ViewPropertyAnimator scaleY(float value) { return add(SCALE_Y, value, false); }
    public ViewPropertyAnimator scaleYBy(float value) { return add(SCALE_Y, value, true); }

    private ViewPropertyAnimator add(int kind, float value, boolean relative) {
        mPending.add(new Step(kind, value, relative));
        if (!mStartScheduled) {
            mStartScheduled = true;
            mView.post(mStarter);
        }
        return this;
    }

    public void start() {
        mView.removeCallbacks(mStarter);
        mStartScheduled = false;
        startNow();
    }

    public void cancel() {
        mView.removeCallbacks(mStarter);
        mStartScheduled = false;
        mPending.clear();
        if (mRunning != null) mRunning.cancel();
    }

    private float read(int kind) {
        switch (kind) {
            case ALPHA: return mView.getAlpha();
            case TRANSLATION_X: return mView.getTranslationX();
            case TRANSLATION_Y: return mView.getTranslationY();
            case TRANSLATION_Z: return mView.getTranslationZ();
            case X: return mView.getX();
            case Y: return mView.getY();
            case Z: return mView.getZ();
            case ROTATION: return mView.getRotation();
            case ROTATION_X: return mView.getRotationX();
            case ROTATION_Y: return mView.getRotationY();
            case SCALE_X: return mView.getScaleX();
            default: return mView.getScaleY();
        }
    }

    private void write(int kind, float value) {
        switch (kind) {
            case ALPHA: mView.setAlpha(value); break;
            case TRANSLATION_X: mView.setTranslationX(value); break;
            case TRANSLATION_Y: mView.setTranslationY(value); break;
            case TRANSLATION_Z: mView.setTranslationZ(value); break;
            case X: mView.setX(value); break;
            case Y: mView.setY(value); break;
            case Z: mView.setZ(value); break;
            case ROTATION: mView.setRotation(value); break;
            case ROTATION_X: mView.setRotationX(value); break;
            case ROTATION_Y: mView.setRotationY(value); break;
            case SCALE_X: mView.setScaleX(value); break;
            default: mView.setScaleY(value); break;
        }
    }

    private void startNow() {
        if (mPending.isEmpty()) return;
        if (mRunning != null) mRunning.cancel();

        final int n = mPending.size();
        final int[] kinds = new int[n];
        final float[] from = new float[n];
        final float[] to = new float[n];
        for (int i = 0; i < n; i++) {
            final Step step = mPending.get(i);
            kinds[i] = step.kind;
            from[i] = read(step.kind);
            to[i] = step.relative ? from[i] + step.value : step.value;
        }
        mPending = new ArrayList<>();

        final ValueAnimator animator = ValueAnimator.ofFloat(0f, 1f);
        animator.setDuration(mDuration);
        animator.setStartDelay(mStartDelay);
        if (mInterpolator != null) animator.setInterpolator(mInterpolator);
        final Runnable startAction = mStartAction;
        final Runnable endAction = mEndAction;
        mStartAction = null;
        mEndAction = null;
        final Animator.AnimatorListener listener = mListener;
        animator.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override
            public void onAnimationUpdate(ValueAnimator a) {
                final float fraction = a.getAnimatedFraction();
                for (int i = 0; i < n; i++) write(kinds[i], from[i] + (to[i] - from[i]) * fraction);
                if (mUpdateListener != null) mUpdateListener.onAnimationUpdate(a);
            }
        });
        animator.addListener(new Animator.AnimatorListener() {
            private boolean mCancelled;

            @Override
            public void onAnimationStart(Animator a) {
                if (startAction != null) startAction.run();
                if (listener != null) listener.onAnimationStart(a);
            }

            @Override
            public void onAnimationCancel(Animator a) {
                mCancelled = true;
                if (listener != null) listener.onAnimationCancel(a);
            }

            @Override
            public void onAnimationRepeat(Animator a) {
                if (listener != null) listener.onAnimationRepeat(a);
            }

            @Override
            public void onAnimationEnd(Animator a) {
                if (mRunning == a) mRunning = null;
                if (listener != null) listener.onAnimationEnd(a);
                if (endAction != null && !mCancelled) endAction.run();
            }
        });
        mRunning = animator;
        animator.start();
    }
}
