package android.view;

public final class ViewTreeObserver {
    public interface OnGlobalLayoutListener { void onGlobalLayout(); }
    public interface OnPreDrawListener { boolean onPreDraw(); }
    public interface OnScrollChangedListener { void onScrollChanged(); }
    public interface OnTouchModeChangeListener { void onTouchModeChanged(boolean isInTouchMode); }
    public interface OnDrawListener { void onDraw(); }

    private final java.util.ArrayList<OnDrawListener> mDrawListeners = new java.util.ArrayList<OnDrawListener>();
    private boolean mDispatchingDraw;

    public void addOnDrawListener(OnDrawListener listener) {
        if (listener == null) throw new NullPointerException();
        if (mDispatchingDraw) throw new IllegalStateException("Cannot add a draw listener during drawing");
        mDrawListeners.add(listener);
    }
    public void removeOnDrawListener(OnDrawListener listener) {
        if (mDispatchingDraw) throw new IllegalStateException("Cannot remove a draw listener during drawing");
        mDrawListeners.remove(listener);
    }
    public void dispatchOnDraw() {
        mDispatchingDraw = true;
        try {
            for (OnDrawListener listener : mDrawListeners) listener.onDraw();
        } finally { mDispatchingDraw = false; }
    }

    public void addOnGlobalLayoutListener(OnGlobalLayoutListener listener) {}
    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener victim) {}
    public void removeGlobalOnLayoutListener(OnGlobalLayoutListener victim) {}
    public void addOnPreDrawListener(OnPreDrawListener listener) {}
    public void removeOnPreDrawListener(OnPreDrawListener victim) {}
    public void addOnScrollChangedListener(OnScrollChangedListener listener) {}
    public void removeOnScrollChangedListener(OnScrollChangedListener victim) {}
    public boolean isAlive() { return true; }
}
