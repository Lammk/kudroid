package android.view;

import java.util.ArrayList;
import java.util.List;

/**
 * android.view.ViewTreeObserver — registers callbacks at lifecycle milestones
 * layout/draw. The lists are actually dispatched from the ViewGroup when layout/draw runs.
 */
public final class ViewTreeObserver {
    /**
     * Call before each drawing. Return false to delay the current frame.
     */
    public interface OnPreDrawListener {
        boolean onPreDraw();
    }

    /**
     * Called after the view tree has been laid out.
     */
    public interface OnGlobalLayoutListener {
        void onGlobalLayout();
    }

    /**
     * Call when window touch mode changes.
     */
    public interface OnTouchModeChangeListener {
        void onTouchModeChanged(boolean isInTouchMode);
    }

    /**
     * Called when the scroll area changes.
     */
    public interface OnScrollChangedListener {
        void onScrollChanged();
    }

    /**
     * Call when window gets/loses focus.
     */
    public interface OnWindowFocusChangeListener {
        void onWindowFocusChanged(boolean hasFocus);
    }

    private final List<OnPreDrawListener> mPreDraw = new ArrayList<OnPreDrawListener>();
    private final List<OnGlobalLayoutListener> mGlobalLayout = new ArrayList<OnGlobalLayoutListener>();
    private final List<OnTouchModeChangeListener> mTouchMode = new ArrayList<OnTouchModeChangeListener>();
    private final List<OnScrollChangedListener> mScroll = new ArrayList<OnScrollChangedListener>();
    private boolean mAlive = true;

    public void addOnPreDrawListener(OnPreDrawListener l) {
        if (l != null && !mPreDraw.contains(l)) mPreDraw.add(l);
    }

    public void removeOnPreDrawListener(OnPreDrawListener l) {
        mPreDraw.remove(l);
    }

    public void addOnGlobalLayoutListener(OnGlobalLayoutListener l) {
        if (l != null && !mGlobalLayout.contains(l)) mGlobalLayout.add(l);
    }

    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener l) {
        mGlobalLayout.remove(l);
    }

    /** Old name before API 16, still called by the app. */
    public void removeGlobalOnLayoutListener(OnGlobalLayoutListener l) {
        mGlobalLayout.remove(l);
    }

    public void addOnTouchModeChangeListener(OnTouchModeChangeListener l) {
        if (l != null && !mTouchMode.contains(l)) mTouchMode.add(l);
    }

    public void removeOnTouchModeChangeListener(OnTouchModeChangeListener l) {
        mTouchMode.remove(l);
    }

    public void addOnScrollChangedListener(OnScrollChangedListener l) {
        if (l != null && !mScroll.contains(l)) mScroll.add(l);
    }

    public void removeOnScrollChangedListener(OnScrollChangedListener l) {
        mScroll.remove(l);
    }

    public boolean isAlive() {
        return mAlive;
    }

    /** Returns false if any listener delays the frame. */
    public boolean dispatchOnPreDraw() {
        boolean proceed = true;
        for (int i = mPreDraw.size() - 1; i >= 0; --i) {
            if (!mPreDraw.get(i).onPreDraw()) proceed = false;
        }
        return proceed;
    }

    public void dispatchOnGlobalLayout() {
        for (int i = mGlobalLayout.size() - 1; i >= 0; --i) {
            mGlobalLayout.get(i).onGlobalLayout();
        }
    }

    public void dispatchOnTouchModeChanged(boolean isInTouchMode) {
        for (int i = mTouchMode.size() - 1; i >= 0; --i) {
            mTouchMode.get(i).onTouchModeChanged(isInTouchMode);
        }
    }

    public void dispatchOnScrollChanged() {
        for (int i = mScroll.size() - 1; i >= 0; --i) {
            mScroll.get(i).onScrollChanged();
        }
    }

    void kill() {
        mAlive = false;
        mPreDraw.clear();
        mGlobalLayout.clear();
        mTouchMode.clear();
        mScroll.clear();
    }

    public interface OnDrawListener {
    }

}
