package android.view;

public final class ViewTreeObserver {
    public interface OnGlobalLayoutListener { void onGlobalLayout(); }
    public interface OnPreDrawListener { boolean onPreDraw(); }
    public interface OnScrollChangedListener { void onScrollChanged(); }
    public interface OnTouchModeChangeListener { void onTouchModeChanged(boolean isInTouchMode); }

    public void addOnGlobalLayoutListener(OnGlobalLayoutListener listener) {}
    public void removeOnGlobalLayoutListener(OnGlobalLayoutListener victim) {}
    public void removeGlobalOnLayoutListener(OnGlobalLayoutListener victim) {}
    public void addOnPreDrawListener(OnPreDrawListener listener) {}
    public void removeOnPreDrawListener(OnPreDrawListener victim) {}
    public void addOnScrollChangedListener(OnScrollChangedListener listener) {}
    public void removeOnScrollChangedListener(OnScrollChangedListener victim) {}
    public boolean isAlive() { return true; }
}
