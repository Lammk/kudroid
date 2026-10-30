package android.view;

/** Directional focus search is not available: no candidate is ever found. */
public class FocusFinder {
    private static final FocusFinder sInstance = new FocusFinder();

    private FocusFinder() {}

    public static FocusFinder getInstance() { return sInstance; }

    public final View findNextFocus(ViewGroup root, View focused, int direction) { return null; }

    public View findNextFocusFromRect(ViewGroup root, android.graphics.Rect focusedRect, int direction) {
        return null;
    }

    public View findNextKeyboardNavigationCluster(View root, View currentCluster, int direction) {
        return null;
    }
}
