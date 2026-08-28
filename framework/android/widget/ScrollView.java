package android.widget;

import android.content.Context;

public class ScrollView extends FrameLayout {
    public ScrollView(Context context) { super(context); }
    public void setFillViewport(boolean fillViewport) {}
    public boolean isFillViewport() { return false; }
    public void smoothScrollTo(int x, int y) { scrollTo(x, y); }
    public void smoothScrollBy(int dx, int dy) { scrollBy(dx, dy); }
}
