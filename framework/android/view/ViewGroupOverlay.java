package android.view;

/** A ViewOverlay that can also hold views. */
public class ViewGroupOverlay extends ViewOverlay {
    private final java.util.ArrayList<View> mViews = new java.util.ArrayList<>();

    ViewGroupOverlay(View host) {
        super(host);
    }

    public void add(View view) {
        if (view != null && !mViews.contains(view)) mViews.add(view);
    }

    public void remove(View view) {
        mViews.remove(view);
    }
}
