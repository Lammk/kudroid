package android.content;

import android.content.pm.PackageManager;
import android.content.pm.SystemPackageManager;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemVibrator;
import android.hardware.SystemSensorManager;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;

public abstract class Context {
    // File and preference modes. MODE_APPEND is the one with a value that matters here —
    // openFileOutput() below tests for it; the rest are the documented constants apps pass
    // to getSharedPreferences and would otherwise fail to resolve at all.
    public static final int MODE_PRIVATE = 0x0000;
    public static final int MODE_WORLD_READABLE = 0x0001;
    public static final int MODE_WORLD_WRITEABLE = 0x0002;
    public static final int MODE_APPEND = 0x8000;
    public static final int MODE_MULTI_PROCESS = 0x0004;
    public static final int MODE_ENABLE_WRITE_AHEAD_LOGGING = 0x0008;
    public static final int MODE_NO_LOCALIZED_COLLATORS = 0x0010;

    public static final String VIBRATOR_SERVICE = "vibrator";
    public static final String VIBRATOR_MANAGER_SERVICE = "vibrator_manager";
    public static final String SENSOR_SERVICE = "sensor";
    public static final String AUDIO_SERVICE = "audio";
    public static final String CONNECTIVITY_SERVICE = "connectivity";
    public static final String WIFI_SERVICE = "wifi";
    public static final String TELEPHONY_SERVICE = "phone";
    public static final String TELEPHONY_SUBSCRIPTION_SERVICE = "telephony_subscription_service";
    public static final String CLIPBOARD_SERVICE = "clipboard";
    public static final String NOTIFICATION_SERVICE = "notification";
    public static final String ALARM_SERVICE = "alarm";
    public static final String POWER_SERVICE = "power";
    public static final String KEYGUARD_SERVICE = "keyguard";
    public static final String WINDOW_SERVICE = "window";
    public static final String LAYOUT_INFLATER_SERVICE = "layout_inflater";

    /**
     * The soft keyboard service.
     *
     * Its absence is what stopped Minecraft: MainActivity.onCreate does
     * {@code getSystemService(INPUT_METHOD_SERVICE)} and throws
     * {@code RuntimeException("Can't get IMM")} when the result is null. The class
     * itself existed all along — only this mapping was missing, so onCreate died and
     * ActivityThread drew its diagnostic screen instead of the game.
     */
    public static final String INPUT_METHOD_SERVICE = "input_method";

    public static final String ACTIVITY_SERVICE = "activity";
    public static final String ACCESSIBILITY_SERVICE = "accessibility";
    public static final String ACCOUNT_SERVICE = "account";
    public static final String APP_OPS_SERVICE = "appops";
    public static final String BLUETOOTH_SERVICE = "bluetooth";
    public static final String DISPLAY_SERVICE = "display";
    public static final String FINGERPRINT_SERVICE = "fingerprint";
    public static final String INPUT_SERVICE = "input";
    public static final String JOB_SCHEDULER_SERVICE = "jobscheduler";
    public static final String LOCATION_SERVICE = "location";
    public static final String SHORTCUT_SERVICE = "shortcut";
    public static final String GRAMMATICAL_INFLECTION_SERVICE = "grammatical_inflection";

    public abstract AssetManager getAssets();
    public abstract Resources getResources();
    public abstract PackageManager getPackageManager();
    public abstract String getPackageName();
    public abstract File getFilesDir();
    public abstract File getCacheDir();
    public abstract File getExternalFilesDir(String type);
    public abstract File getExternalCacheDir();
    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    // Declared here because resolution walks the class hierarchy, and ApplicationContext
    // implementing them is not enough on its own: a caller holding an Activity resolves
    // against Activity -> ContextWrapper -> Context, so a method missing from those two
    // is a NoSuchMethodError even though the implementation exists further down.
    //
    // That is not hypothetical. GameActivity.onCreate calls getObbDir(), which threw
    // NoSuchMethodError and aborted Activity creation for Minecraft — the game then sat
    // on a black screen because the Looper was running an Activity that never finished
    // being created. The other four are on the same path and would each have been the
    // next launch to fail.
    public abstract File getObbDir();
    public abstract File getDir(String name, int mode);
    public abstract File getDatabasePath(String name);
    public abstract File getCodeCacheDir();
    public abstract File getNoBackupFilesDir();

    public String getPackageCodePath() {
        return "/data/app/" + getPackageName() + "/base.apk";
    }

    public String getPackageResourcePath() {
        return getPackageCodePath();
    }

    // Needed by all five real APKs in the corpus. Concrete rather than abstract so
    // ContextWrapper and ApplicationContext do not both have to restate them; the base
    // answers are correct for every context KuDroid creates.

    /**
     * The ContentResolver for this context.
     *
     * One per process, not per context: apps register observers through one resolver and
     * expect notifications from another, and separate instances would silently drop them.
     */
    public ContentResolver getContentResolver() {
        return ContentResolver.getInstance();
    }

    /** Describes this application, as the manifest declares it. */
    public android.content.pm.ApplicationInfo getApplicationInfo() {
        android.content.pm.ApplicationInfo info = new android.content.pm.ApplicationInfo();
        info.packageName = getPackageName();
        info.processName = getPackageName();
        info.dataDir = "/data/data/" + getPackageName();
        return info;
    }

    /**
     * Whether this app holds a permission.
     *
     * Granted, which matches what PermissionManager reports elsewhere in KuDroid and what
     * an installed-by-the-user app would see on Android. Denying here would send apps down
     * their request-permission path, which needs UI KuDroid does not present — so they
     * would wait for a dialog that never appears.
     */
    public int checkCallingOrSelfPermission(String permission) {
        return android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    public int checkPermission(String permission, int pid, int uid) {
        return android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    /** The main thread's Looper, which every lifecycle callback runs on. */
    public android.os.Looper getMainLooper() {
        return android.os.Looper.getMainLooper();
    }

    /**
     * Data directory for this app.
     *
     * The same path getFilesDir()'s parent would give, but named the way modern apps ask
     * for it. Both go through the VFS, so either spelling lands in the same place.
     */
    public File getDataDir() {
        return new File("/data/data/" + getPackageName());
    }

    /**
     * Single-element arrays for the plural forms.
     *
     * Android returns one entry per storage volume and KuDroid has one, so a single-element
     * array is the honest answer. Returning an empty array would be worse than wrong: apps
     * index [0] without checking.
     */
    public File[] getExternalFilesDirs(String type) {
        return new File[] { getExternalFilesDir(type) };
    }

    public File[] getExternalCacheDirs() {
        return new File[] { getExternalCacheDir() };
    }

    public File[] getObbDirs() {
        return new File[] { getObbDir() };
    }

    public File[] getExternalMediaDirs() {
        return new File[] { getExternalFilesDir("Media") };
    }

    /** Open a file in getFilesDir(), the shorthand apps use for app-private storage. */
    public java.io.FileInputStream openFileInput(String name) throws java.io.FileNotFoundException {
        return new java.io.FileInputStream(new File(getFilesDir(), name));
    }

    public java.io.FileOutputStream openFileOutput(String name, int mode)
            throws java.io.FileNotFoundException {
        // MODE_APPEND is 0x8000; anything else truncates, as on Android.
        final boolean append = (mode & 0x8000) != 0;
        return new java.io.FileOutputStream(new File(getFilesDir(), name), append);
    }

    public boolean deleteFile(String name) {
        return new File(getFilesDir(), name).delete();
    }

    public String[] fileList() {
        final String[] names = getFilesDir().list();
        return names != null ? names : new String[0];
    }

    /**
     * The package name to attribute operations to, which for a normal app is its own.
     * Attribution tags are a privacy-accounting feature with nothing behind them here.
     */
    public String getOpPackageName() {
        return getPackageName();
    }

    public String getAttributionTag() {
        return null;
    }

    /**
     * Names asked for that KuDroid has no manager for, reported once each.
     *
     * A null return from getSystemService is the quietest possible failure: the app
     * either dereferences it and dies somewhere unrelated, or throws its own opaque
     * message — "Can't get IMM" says nothing about which service name was involved
     * or that the class was actually present. Naming the miss turns each of these
     * into a one-line fix instead of a debugging session.
     */
    private static final java.util.Set<String> sReportedMissingServices =
            java.util.Collections.synchronizedSet(new java.util.HashSet<String>());

    /**
     * The manager object for a service name, or null when there is none.
     *
     * Every branch here has to stay in step with the classes under framework/: a
     * manager class that exists but is not reachable through this method is
     * indistinguishable, from the app's side, from one that was never written.
     * INPUT_METHOD_SERVICE and ACTIVITY_SERVICE were both in that state — the
     * classes shipped, the mapping did not.
     */
    public Object getSystemService(String name) {
        if (name == null) return null;

        if (name.equals(WINDOW_SERVICE)) return new android.view.WindowManagerImpl();
        if (name.equals(LAYOUT_INFLATER_SERVICE)) return android.view.LayoutInflater.from(this);
        if (name.equals(INPUT_METHOD_SERVICE)) return InputMethodManagerHolder.get();
        if (name.equals(SENSOR_SERVICE)) return new SystemSensorManager();
        if (name.equals(AUDIO_SERVICE)) return new android.media.AudioManager();
        if (name.equals(VIBRATOR_SERVICE)) return new SystemVibrator();
        if (name.equals(CONNECTIVITY_SERVICE)) return new android.net.ConnectivityManager();
        if (name.equals(WIFI_SERVICE)) return new android.net.wifi.WifiManager();
        if (name.equals(TELEPHONY_SERVICE)) return new android.telephony.TelephonyManager();
        if (name.equals(CLIPBOARD_SERVICE)) return new ClipboardManager();
        if (name.equals(NOTIFICATION_SERVICE)) return new android.app.NotificationManager();
        if (name.equals(ALARM_SERVICE)) return new android.app.AlarmManager();
        if (name.equals(POWER_SERVICE)) return new android.os.PowerManager();
        if (name.equals(KEYGUARD_SERVICE)) return new android.app.KeyguardManager();

        // Managers that shipped under framework/ without ever being reachable.
        if (name.equals(ACTIVITY_SERVICE)) return new android.app.ActivityManager();
        if (name.equals(ACCESSIBILITY_SERVICE)) {
            return android.view.accessibility.AccessibilityManager.getInstance(this);
        }
        if (name.equals(ACCOUNT_SERVICE)) return android.accounts.AccountManager.get(this);
        if (name.equals(APP_OPS_SERVICE)) return new android.app.AppOpsManager();
        if (name.equals(BLUETOOTH_SERVICE)) return new android.bluetooth.BluetoothManager();
        if (name.equals(DISPLAY_SERVICE)) return new android.hardware.display.DisplayManager();
        if (name.equals(FINGERPRINT_SERVICE)) {
            return new android.hardware.fingerprint.FingerprintManager();
        }
        if (name.equals(INPUT_SERVICE)) return new android.hardware.input.InputManager();
        if (name.equals(JOB_SCHEDULER_SERVICE)) return new android.app.job.JobScheduler();
        if (name.equals(LOCATION_SERVICE)) return new android.location.LocationManager();
        if (name.equals(SHORTCUT_SERVICE)) return new android.content.pm.ShortcutManager();
        if (name.equals(TELEPHONY_SUBSCRIPTION_SERVICE)) {
            return new android.telephony.SubscriptionManager(this);
        }
        if (name.equals(GRAMMATICAL_INFLECTION_SERVICE)) {
            return new android.app.GrammaticalInflectionManager();
        }
        // VibratorManager wraps a Vibrator on Android 12+; apps target either.
        if (name.equals(VIBRATOR_MANAGER_SERVICE)) return new SystemVibrator();

        reportUnknownService(name);
        return null;
    }

    /**
     * Name a service KuDroid does not provide, once per distinct name.
     *
     * Deliberately not an exception: Android returns null for a service the device
     * does not have and apps are written to cope, so throwing would break code that
     * is behaving correctly. The log line is the whole remedy.
     */
    private static void reportUnknownService(String name) {
        if (!sReportedMissingServices.add(name)) return;
        android.util.Log.w("Context",
                "MISSING_SYSTEM_SERVICE: getSystemService(\"" + name + "\") -> null"
                + " (no manager for this name; add it to Context.getSystemService)");
    }

    /**
     * One InputMethodManager per process.
     *
     * AOSP's is a singleton and apps rely on that: they cache the instance, compare
     * it against a later getSystemService result, and route all soft-keyboard state
     * through the one object. Handing out a fresh instance per call would make
     * showSoftInput and the InputConnection disagree about what is focused.
     *
     * A holder class rather than a static field on Context, so the instance is
     * created on first use — Context is initialised long before any input exists.
     */
    private static final class InputMethodManagerHolder {
        private static android.view.inputmethod.InputMethodManager sInstance;

        static synchronized android.view.inputmethod.InputMethodManager get() {
            if (sInstance == null) {
                sInstance = android.view.inputmethod.InputMethodManager.getInstance();
            }
            return sInstance;
        }
    }

    public CharSequence getText(int resId) {
        Resources r = getResources();
        return r != null ? r.getText(resId) : "";
    }
    public String getString(int resId) {
        Resources r = getResources();
        return r != null ? r.getString(resId) : "";
    }
    public String getString(int resId, Object... formatArgs) {
        Resources r = getResources();
        return r != null ? r.getString(resId, formatArgs) : "";
    }
    public int checkSelfPermission(String permission) { return PackageManager.PERMISSION_GRANTED; }
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) { return null; }
    public void unregisterReceiver(BroadcastReceiver receiver) {}
    public void sendBroadcast(Intent intent) {}
    public void startActivity(Intent intent) {}
    public void startActivity(Intent intent, Bundle options) {}
    public Context getApplicationContext() { return this; }

    /**
     * The theme applied to this Context.
     *
     * Never null. Returning null — which happened while Resources.Theme did not
     * exist — breaks every caller, because the idiom is to chain
     * {@code getTheme().resolveAttribute(...)} or
     * {@code getTheme().obtainStyledAttributes(...)} without a null check. Both run
     * during onCreate on any app built with AppCompat or androidx.core.splashscreen.
     */
    public Resources.Theme getTheme() {
        Resources r = getResources();
        if (r == null) r = Resources.getSystem();
        return r.getDefaultTheme();
    }

    public void setTheme(int resid) {}

    public android.content.res.TypedArray obtainStyledAttributes(int[] attrs) {
        return getTheme().obtainStyledAttributes(attrs);
    }

    public android.content.res.TypedArray obtainStyledAttributes(int resid, int[] attrs) {
        return getTheme().obtainStyledAttributes(resid, attrs);
    }

    public android.content.res.TypedArray obtainStyledAttributes(
            android.util.AttributeSet set, int[] attrs) {
        return getTheme().obtainStyledAttributes(set, attrs, 0, 0);
    }

    public android.content.res.TypedArray obtainStyledAttributes(
            android.util.AttributeSet set, int[] attrs, int defStyleAttr, int defStyleRes) {
        return getTheme().obtainStyledAttributes(set, attrs, defStyleAttr, defStyleRes);
    }

    public ClassLoader getClassLoader() {
        return ClassLoader.getSystemClassLoader();
    }

    public android.graphics.drawable.Drawable getDrawable(int id) {
        Resources r = getResources();
        return r != null ? r.getDrawable(id, getTheme()) : null;
    }

    public int getColor(int id) {
        Resources r = getResources();
        return r != null ? r.getColor(id, getTheme()) : 0xFF000000;
    }

    public boolean isRestricted() { return false; }
}
