package android.view;

import android.graphics.Rect;
import java.util.List;
import java.util.Collections;

public final class DisplayCutout {
    public DisplayCutout(Rect safeInsets, List<Rect> boundingRects) {}
    public int getSafeInsetTop() { return 0; }
    public int getSafeInsetBottom() { return 0; }
    public int getSafeInsetLeft() { return 0; }
    public int getSafeInsetRight() { return 0; }
    public List<Rect> getBoundingRects() { return Collections.emptyList(); }
}
