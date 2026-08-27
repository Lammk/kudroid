package android.content;

import android.os.Bundle;

/**
 * minimal android.content.context implementation.
 *
 * provides access to application resources, sharing options and services
 * other systems. for kudroid minimal framework, most methods return
 * default value or null so the app doesn't crash during startup.
 */
public abstract class Context {
    /** file mode: world-readable. */
    public static final int MODE_WORLD_READABLE = 0x00000001;
    /** file mode: world-writable. */
    public static final int MODE_WORLD_WRITEABLE = 0x00000002;
    /** file mode: append. */
    public static final int MODE_APPEND = 0x00008000;
    /** file mode: private. */
    public static final int MODE_PRIVATE = 0x00000000;

    /**
     * returns application context.
     */
    public abstract Context getApplicationContext();

    /**
     * returns the package name.
     */
    public abstract String getPackageName();

    /**
     * returns the app's sharing options.
     */
    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    /**
     * returns a system service by name.
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
     * returns a string resource.
     */
    public String getString(int resId) {
        return "";
    }

    /**
     * returns a string resource with format arguments.
     */
    public String getString(int resId, Object... formatArgs) {
        return "";
    }

    /**
     * start an activity.
     */
    public void startActivity(Intent intent) {
    }

    /**
     * returns the application's properties.
     */
    public android.content.res.AssetManager getAssets() {
        return new android.content.res.AssetManager();
    }

    /**
     * returns the application's resources.
     */
    public android.content.res.Resources getResources() {
        return new android.content.res.Resources();
    }

    /**
     * returns content resolver.
     */
    public android.content.ContentResolver getContentResolver() {
        return new android.content.ContentResolver(this);
    }

    /**
     * returns the main iterator.
     */
    public android.os.Looper getMainLooper() {
        return android.os.Looper.getMainLooper();
    }

    /**
     * returns package manager.
     */
    public android.content.pm.PackageManager getPackageManager() {
        return new android.content.pm.PackageManager();
    }

    /**
     * returns application information.
     */
    public android.content.pm.ApplicationInfo getApplicationInfo() {
        return new android.content.pm.ApplicationInfo();
    }

    /**
     * returns class loader.
     */
    public ClassLoader getClassLoader() {
        return Context.class.getClassLoader();
    }

    /**
     * returns the application's file directory.
     */
    public java.io.File getFilesDir() {
        return new java.io.File("/data/data/" + getPackageName() + "/files");
    }

    /**
     * returns the application cache directory.
     */
    public java.io.File getCacheDir() {
        return new java.io.File("/data/data/" + getPackageName() + "/cache");
    }

    /**
     * returns the application's external file directory.
     */
    public java.io.File getExternalFilesDir(String type) {
        return new java.io.File("/sdcard/Android/data/" + getPackageName() + "/files");
    }

    /**
     * returns the application's database path.
     */
    public java.io.File getDatabasePath(String name) {
        return new java.io.File("/data/data/" + getPackageName() + "/databases/" + name);
    }

    /**
     * returns the application's shared preferences directory.
     */
    public java.io.File getSharedPrefsFile(String name) {
        return new java.io.File("/data/data/" + getPackageName() + "/shared_prefs/" + name + ".xml");
    }

    /**
     * returns the application's obb directory.
     */
    public java.io.File getObbDir() {
        return new java.io.File("/sdcard/Android/obb/" + getPackageName());
    }
}
