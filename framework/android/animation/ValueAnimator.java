package android.animation;

public class ValueAnimator extends Animator {
    private long mDuration = 300;
    private TimeInterpolator mInterpolator = new android.view.animation.LinearInterpolator();
    private boolean mRunning = false;

    public static ValueAnimator ofFloat(float... values) { return new ValueAnimator(); }
    public static ValueAnimator ofInt(int... values) { return new ValueAnimator(); }
    public static ValueAnimator ofObject(TypeEvaluator evaluator, Object... values) { return new ValueAnimator(); }
    public void start() { mRunning = true; }
    public void cancel() { mRunning = false; }
    public void end() { mRunning = false; }
    public long getDuration() { return mDuration; }
    public ValueAnimator setDuration(long duration) { mDuration = duration; return this; }
    public void setInterpolator(TimeInterpolator value) { mInterpolator = value; }
    public boolean isRunning() { return mRunning; }
    public Object getAnimatedValue() { return 0.0f; }
}
