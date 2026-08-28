package android.hardware.display;

import android.view.Display;
import android.content.Context;

public final class DisplayManager {
    public DisplayManager() {}
    public Display getDisplay(int displayId) { return new Display(); }
    public Display[] getDisplays() { return new Display[]{ new Display() }; }
}
