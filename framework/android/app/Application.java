package android.app;

import android.content.Context;
import android.content.ContextWrapper;

/**
 * minimal android.app.application implementation.
 *
 * base class for the application. for kudroid minimal framework, this class
 * provides oncreate lifecycle callback.
 */
public class Application extends ContextWrapper {
    public Application() {
        super(null);
    }

    /**
     * is called when the application is starting.
     */
    public void onCreate() {
    }

    /**
     * is called when the application is low on memory.
     */
    public void onLowMemory() {
    }

    /**
     * is called when the application is reduced to memory.
     */
    public void onTrimMemory(int level) {
    }

    /**
     * attaches the base context (called by the framework).
     */
    public void attach(Context base) {
        attachBaseContext(base);
    }

    public interface ActivityLifecycleCallbacks {
        void onActivityCreated(Activity activity, android.os.Bundle savedInstanceState);
        void onActivityStarted(Activity activity);
        void onActivityResumed(Activity activity);
        void onActivityPaused(Activity activity);
        void onActivityStopped(Activity activity);
        void onActivitySaveInstanceState(Activity activity, android.os.Bundle outState);
        void onActivityDestroyed(Activity activity);
    }

    public void registerActivityLifecycleCallbacks(ActivityLifecycleCallbacks callback) {
    }

    public void unregisterActivityLifecycleCallbacks(ActivityLifecycleCallbacks callback) {
    }

}
