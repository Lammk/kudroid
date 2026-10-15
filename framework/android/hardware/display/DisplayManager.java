package android.hardware.display;

import android.view.Display;
import android.content.Context;
import android.os.Handler;

public final class DisplayManager {
    public interface DisplayListener {
        void onDisplayAdded(int displayId);
        void onDisplayRemoved(int displayId);
        void onDisplayChanged(int displayId);
    }

    public DisplayManager() {}
    public Display getDisplay(int displayId) { return new Display(); }
    public Display[] getDisplays() { return new Display[]{ new Display() }; }
    public void registerDisplayListener(DisplayListener listener, Handler handler) {}
    public void unregisterDisplayListener(DisplayListener listener) {}
}
