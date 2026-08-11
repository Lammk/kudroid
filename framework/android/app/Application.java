package android.app;

import android.content.Context;
import android.content.ContextWrapper;

/**
 * Minimal android.app.Application implementation.
 *
 * The base class for the application. For KuDroid's minimal framework, this
 * provides the onCreate lifecycle callback.
 */
public class Application extends ContextWrapper {
    public Application() {
        super(null);
    }

    /**
     * Called when the application is starting.
     */
    public void onCreate() {
    }

    /**
     * Called when the application is low on memory.
     */
    public void onLowMemory() {
    }

    /**
     * Called when the application is trimmed of memory.
     */
    public void onTrimMemory(int level) {
    }

    /**
     * Attach the base context (invoked by the framework).
     */
    public void attach(Context base) {
        attachBaseContext(base);
    }
}
