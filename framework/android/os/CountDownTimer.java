package android.os;

public abstract class CountDownTimer {
    private final long mMillisInFuture;
    private final long mCountdownInterval;
    private boolean mCancelled = false;

    public CountDownTimer(long millisInFuture, long countDownInterval) {
        mMillisInFuture = millisInFuture;
        mCountdownInterval = countDownInterval;
    }
    public synchronized final void cancel() {
        mCancelled = true;
    }
    public synchronized final CountDownTimer start() {
        mCancelled = false;
        if (mMillisInFuture <= 0) {
            onFinish();
            return this;
        }
        onTick(mMillisInFuture);
        onFinish();
        return this;
    }
    public abstract void onTick(long millisUntilFinished);
    public abstract void onFinish();
}
