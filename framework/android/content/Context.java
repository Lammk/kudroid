package android.content;

import android.os.Bundle;

/**
 * Minimal android.content.Context implementation.
 *
 * Provides access to application resources, shared preferences, and other
 * system services. For KuDroid's minimal framework, most methods return
 * defaults or null so apps don't crash during startup.
 */
public abstract class Context {
    /** File mode: world-readable. */
    public static final int MODE_WORLD_READABLE = 0x00000001;
    /** File mode: world-writable. */
    public static final int MODE_WORLD_WRITEABLE = 0x00000002;
    /** File mode: append. */
    public static final int MODE_APPEND = 0x00008000;
    /** File mode: private. */
    public static final int MODE_PRIVATE = 0x00000000;

    /**
     * Return the application context.
     */
    public abstract Context getApplicationContext();

    /**
     * Return the package name.
     */
    public abstract String getPackageName();

    /**
     * Return the application's shared preferences.
     */
    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    /**
     * Return a system service by name.
     */
    public Object getSystemService(String name) {
        if (name == null) return null;
        if (name.equals("telephony")) return new android.telephony.TelephonyManager();
        if (name.equals("bluetooth")) return android.bluetooth.BluetoothAdapter.getDefaultAdapter();
        if (name.equals("notification")) return new android.app.NotificationManager();
        if (name.equals("location")) return new android.location.LocationManager();
        if (name.equals("wifi")) return new android.net.wifi.WifiManager();
        if (name.equals("sensor")) return new android.hardware.SensorManager();
        if (name.equals("audio")) return new android.media.AudioManager();
        if (name.equals("vibrator")) return new android.os.Vibrator();
        if (name.equals("power")) return new android.os.PowerManager();
        if (name.equals("connectivity")) return new android.net.ConnectivityManager();
        if (name.equals("window")) return new android.view.WindowManager();
        if (name.equals("layout_inflater")) return new android.view.LayoutInflater();
        if (name.equals("activity")) return this;
        if (name.equals("clipboard")) return new android.content.ClipboardManager();
        if (name.equals("input_method")) return new android.view.inputmethod.InputMethodManager();
        return null;
    }

    /**
     * Return a string resource.
     */
    public String getString(int resId) {
        return "";
    }

    /**
     * Return a string resource with format args.
     */
    public String getString(int resId, Object... formatArgs) {
        return "";
    }

    /**
     * Start an activity.
     */
    public void startActivity(Intent intent) {
    }

    /**
     * Return the application's assets.
     */
    public android.content.res.AssetManager getAssets() {
        return new android.content.res.AssetManager();
    }

    /**
     * Return the application's resources.
     */
    public android.content.res.Resources getResources() {
        return new android.content.res.Resources();
    }

    /**
     * Return the content resolver.
     */
    public android.content.ContentResolver getContentResolver() {
        return new android.content.ContentResolver(this);
    }

    /**
     * Return the main looper.
     */
    public android.os.Looper getMainLooper() {
        return android.os.Looper.getMainLooper();
    }

    /**
     * Return the package manager.
     */
    public android.content.pm.PackageManager getPackageManager() {
        return new android.content.pm.PackageManager();
    }

    /**
     * Return the application info.
     */
    public android.content.pm.ApplicationInfo getApplicationInfo() {
        return new android.content.pm.ApplicationInfo();
    }

    /**
     * Return the class loader.
     */
    public ClassLoader getClassLoader() {
        return Context.class.getClassLoader();
    }

    /**
     * Return the application's files directory.
     */
    public java.io.File getFilesDir() {
        return new java.io.File("/data/data/" + getPackageName() + "/files");
    }

    /**
     * Return the application's cache directory.
     */
    public java.io.File getCacheDir() {
        return new java.io.File("/data/data/" + getPackageName() + "/cache");
    }

    /**
     * Return the application's external files directory.
     */
    public java.io.File getExternalFilesDir(String type) {
        return new java.io.File("/sdcard/Android/data/" + getPackageName() + "/files");
    }

    /**
     * Return the application's database path.
     */
    public java.io.File getDatabasePath(String name) {
        return new java.io.File("/data/data/" + getPackageName() + "/databases/" + name);
    }

    /**
     * Return the application's shared preferences directory.
     */
    public java.io.File getSharedPrefsFile(String name) {
        return new java.io.File("/data/data/" + getPackageName() + "/shared_prefs/" + name + ".xml");
    }

    /**
     * Return the application's obb directory.
     */
    public java.io.File getObbDir() {
        return new java.io.File("/sdcard/Android/obb/" + getPackageName());
    }
}
