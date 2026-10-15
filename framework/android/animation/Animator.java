package android.animation;

import java.util.ArrayList;

public abstract class Animator implements Cloneable {
    public interface AnimatorListener {
        void onAnimationStart(Animator animation);
        void onAnimationEnd(Animator animation);
        void onAnimationCancel(Animator animation);
        void onAnimationRepeat(Animator animation);
    }
    private final ArrayList<AnimatorListener> mListeners = new ArrayList<AnimatorListener>();

    public void start() {}
    public void cancel() {}
    public void end() {}
    public abstract long getDuration();
    public abstract Animator setDuration(long duration);
    public abstract void setInterpolator(TimeInterpolator value);
    public abstract boolean isRunning();
    public void addListener(AnimatorListener listener) { if (listener != null) mListeners.add(listener); }
    public void removeListener(AnimatorListener listener) { mListeners.remove(listener); }
}
