package android.app;

import android.content.Context;
import android.content.ContextWrapper;

/**
 * minimal android.app.application implementation.
 *
 * base class for the application. for kudroid minimal framework, this class
 * provides oncreate lifecycle callback.
 */
public class Application extends ContextWrapper implements android.content.ComponentCallbacks2 {
    public Application() {
        super(null);
    }

    public static String getProcessName() {
        String name = ActivityThread.getPackageName();
        return name == null || name.isEmpty() ? null : name;
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
        for (android.content.ComponentCallbacks c : callbacksSnapshot()) {
            c.onLowMemory();
        }
    }

    public void onConfigurationChanged(android.content.res.Configuration newConfig) {
        for (android.content.ComponentCallbacks c : callbacksSnapshot()) {
            c.onConfigurationChanged(newConfig);
        }
    }

    /**
     * is called when the application is reduced to memory.
     */
    public void onTrimMemory(int level) {
        for (android.content.ComponentCallbacks c : callbacksSnapshot()) {
            if (c instanceof android.content.ComponentCallbacks2) {
                ((android.content.ComponentCallbacks2) c).onTrimMemory(level);
            }
        }
    }

    private final java.util.ArrayList<android.content.ComponentCallbacks> mComponentCallbacks =
            new java.util.ArrayList<android.content.ComponentCallbacks>();

    private android.content.ComponentCallbacks[] callbacksSnapshot() {
        synchronized (mComponentCallbacks) {
            return mComponentCallbacks.toArray(
                    new android.content.ComponentCallbacks[mComponentCallbacks.size()]);
        }
    }

    @Override
    public void registerComponentCallbacks(android.content.ComponentCallbacks callback) {
        synchronized (mComponentCallbacks) {
            mComponentCallbacks.add(callback);
        }
    }

    @Override
    public void unregisterComponentCallbacks(android.content.ComponentCallbacks callback) {
        synchronized (mComponentCallbacks) {
            mComponentCallbacks.remove(callback);
        }
    }

    /**
     * attaches the base context (called by the framework).
     */
    public void attach(Context base) {
        attachBaseContext(base);
    }

    final ActivityLifecycleDispatcher lifecycleCallbacks = new ActivityLifecycleDispatcher();

    public interface ActivityLifecycleCallbacks {
        void onActivityCreated(Activity activity, android.os.Bundle savedInstanceState);
        default void onActivityPreCreated(Activity activity, android.os.Bundle savedInstanceState) {}
        default void onActivityPostCreated(Activity activity, android.os.Bundle savedInstanceState) {}
        void onActivityStarted(Activity activity);
        default void onActivityPreStarted(Activity activity) {}
        default void onActivityPostStarted(Activity activity) {}
        void onActivityResumed(Activity activity);
        default void onActivityPreResumed(Activity activity) {}
        default void onActivityPostResumed(Activity activity) {}
        void onActivityPaused(Activity activity);
        default void onActivityPrePaused(Activity activity) {}
        default void onActivityPostPaused(Activity activity) {}
        void onActivityStopped(Activity activity);
        default void onActivityPreStopped(Activity activity) {}
        default void onActivityPostStopped(Activity activity) {}
        void onActivitySaveInstanceState(Activity activity, android.os.Bundle outState);
        default void onActivityPreSaveInstanceState(Activity activity, android.os.Bundle outState) {}
        default void onActivityPostSaveInstanceState(Activity activity, android.os.Bundle outState) {}
        void onActivityDestroyed(Activity activity);
        default void onActivityPreDestroyed(Activity activity) {}
        default void onActivityPostDestroyed(Activity activity) {}
    }

    public void registerActivityLifecycleCallbacks(ActivityLifecycleCallbacks callback) {
        lifecycleCallbacks.add(callback);
    }

    public void unregisterActivityLifecycleCallbacks(ActivityLifecycleCallbacks callback) {
        lifecycleCallbacks.remove(callback);
    }
}
