package android.os;

public abstract class CountDownTimer {
    private final long mMillisInFuture;
    private final long mCountdownInterval;
    private boolean mCancelled = false;
    private Handler mHandler;

    public CountDownTimer(long millisInFuture, long countDownInterval) {
        mMillisInFuture = millisInFuture;
        mCountdownInterval = countDownInterval;
    }
    public synchronized final void cancel() {
        mCancelled = true;
        if (mHandler != null) {
            mHandler.removeCallbacksAndMessages(null);
        }
    }
    public synchronized final CountDownTimer start() {
        mCancelled = false;
        if (mMillisInFuture <= 0) {
            onFinish();
            return this;
        }
        mHandler = new Handler(Looper.getMainLooper());
        scheduleTick(mMillisInFuture);
        return this;
    }
    private void scheduleTick(final long millisLeft) {
        if (mCancelled) return;
        if (millisLeft <= 0) {
            mHandler.post(new Runnable() {
                public void run() {
                    if (!mCancelled) onFinish();
                }
            });
            return;
        }
        mHandler.postDelayed(new Runnable() {
            public void run() {
                if (mCancelled) return;
                onTick(millisLeft);
                scheduleTick(millisLeft - mCountdownInterval);
            }
        }, Math.min(millisLeft, mCountdownInterval));
    }
    public abstract void onTick(long millisUntilFinished);
    public abstract void onFinish();
}
