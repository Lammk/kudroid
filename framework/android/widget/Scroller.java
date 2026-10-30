package android.widget;

import android.content.Context;
import android.view.animation.Interpolator;

/** The original scroller API, backed by {@link OverScroller}. */
public class Scroller {
    private final OverScroller mScroller;

    public Scroller(Context context) {
        this(context, null);
    }

    public Scroller(Context context, Interpolator interpolator) {
        mScroller = new OverScroller(context, interpolator);
    }

    public Scroller(Context context, Interpolator interpolator, boolean flywheel) {
        this(context, interpolator);
    }

    public final boolean isFinished() { return mScroller.isFinished(); }
    public final void forceFinished(boolean finished) { mScroller.forceFinished(finished); }
    public final int getDuration() { return mScroller.getDuration(); }
    public final int getCurrX() { return mScroller.getCurrX(); }
    public final int getCurrY() { return mScroller.getCurrY(); }
    public float getCurrVelocity() { return mScroller.getCurrVelocity(); }
    public final int getStartX() { return mScroller.getStartX(); }
    public final int getStartY() { return mScroller.getStartY(); }
    public final int getFinalX() { return mScroller.getFinalX(); }
    public final int getFinalY() { return mScroller.getFinalY(); }
    public final void setFriction(float friction) { mScroller.setFriction(friction); }
    public boolean computeScrollOffset() { return mScroller.computeScrollOffset(); }
    public void startScroll(int startX, int startY, int dx, int dy) { mScroller.startScroll(startX, startY, dx, dy); }
    public void startScroll(int startX, int startY, int dx, int dy, int duration) {
        mScroller.startScroll(startX, startY, dx, dy, duration);
    }
    public void fling(int startX, int startY, int velocityX, int velocityY, int minX, int maxX, int minY, int maxY) {
        mScroller.fling(startX, startY, velocityX, velocityY, minX, maxX, minY, maxY);
    }
    public void abortAnimation() { mScroller.abortAnimation(); }
    public void extendDuration(int extend) { mScroller.extendDuration(extend); }
    public int timePassed() { return mScroller.timePassed(); }
    public void setFinalX(int newX) { mScroller.setFinalX(newX); }
    public void setFinalY(int newY) { mScroller.setFinalY(newY); }
}
