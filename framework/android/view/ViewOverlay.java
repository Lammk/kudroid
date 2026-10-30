package android.view;

import android.graphics.drawable.Drawable;

import java.util.ArrayList;

/** Extra drawables shown on top of a view. */
public class ViewOverlay {
    final ArrayList<Drawable> mDrawables = new ArrayList<>();
    final View mHost;

    ViewOverlay(View host) {
        mHost = host;
    }

    public void add(Drawable drawable) {
        if (drawable != null && !mDrawables.contains(drawable)) {
            mDrawables.add(drawable);
            mHost.invalidate();
        }
    }

    public void remove(Drawable drawable) {
        if (mDrawables.remove(drawable)) mHost.invalidate();
    }

    public void clear() {
        mDrawables.clear();
        mHost.invalidate();
    }

    boolean isEmpty() { return mDrawables.isEmpty(); }
}
