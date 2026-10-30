package android.widget;

import android.content.Context;
import android.graphics.drawable.Animatable;
import android.util.AttributeSet;

/**
 * Switches between children on a timer. The timer only advances the visible child;
 * per-item animations are not required for the view to be usable.
 */
public class ViewFlipper extends ViewAnimator implements Animatable {

    private int mFlipInterval = 3000;
    private boolean mAutoStart = false;
    private boolean mRunning = false;
    private boolean mStarted = false;
    private boolean mVisible = false;

    private final Runnable mFlipRunnable = new Runnable() {
        @Override
        public void run() {
            if (!mRunning) return;
            showNext();
            postDelayed(this, mFlipInterval);
        }
    };

    public ViewFlipper(Context context) {
        super(context);
    }

    public ViewFlipper(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public ViewFlipper(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public ViewFlipper(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
    }

    public void setFlipInterval(int milliseconds) {
        mFlipInterval = Math.max(0, milliseconds);
    }

    public int getFlipInterval() {
        return mFlipInterval;
    }

    public void startFlipping() {
        mStarted = true;
        updateRunning();
    }

    public void stopFlipping() {
        mStarted = false;
        updateRunning();
    }

    public void setAutoStart(boolean autoStart) {
        mAutoStart = autoStart;
    }

    public boolean isAutoStart() {
        return mAutoStart;
    }

    @Override
    public void start() {
        startFlipping();
    }

    @Override
    public void stop() {
        stopFlipping();
    }

    @Override
    public boolean isRunning() {
        return mRunning;
    }

    public boolean isFlipping() {
        return mRunning;
    }

    public void setVisible(boolean visible) {
        setVisibility(visible ? VISIBLE : INVISIBLE);
    }

    @Override
    public void setVisibility(int visibility) {
        super.setVisibility(visibility);
        mVisible = visibility == VISIBLE;
        updateRunning();
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (mAutoStart && !mRunning) {
            startFlipping();
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        stopFlipping();
        super.onDetachedFromWindow();
    }

    @Override
    protected void onWindowVisibilityChanged(int visibility) {
        super.onWindowVisibilityChanged(visibility);
        mVisible = visibility == VISIBLE;
        updateRunning();
    }

    private void updateRunning() {
        final boolean running = mVisible && mStarted;
        if (running == mRunning) return;
        mRunning = running;
        removeCallbacks(mFlipRunnable);
        if (mRunning) {
            postDelayed(mFlipRunnable, mFlipInterval);
        }
    }
}
