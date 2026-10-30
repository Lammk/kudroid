package android.app;

/**
 * Android AppGlobals entry.
 */
public class AppGlobals {
    public static Application getInitialApplication() {
        return ActivityThread.currentApplication();
    }

    public static String getInitialPackage() {
        return ActivityThread.getPackageName();
    }

    public static float getFloatCoreSetting(String key, float def) {
        return def;
    }

    public static int getIntCoreSetting(String key, int def) {
        return def;
    }
}
