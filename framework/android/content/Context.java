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
    public static final String VIBRATOR_SERVICE = "vibrator";
    public static final String SENSOR_SERVICE = "sensor";
    public static final String AUDIO_SERVICE = "audio";
    public static final String CONNECTIVITY_SERVICE = "connectivity";
    public static final String WIFI_SERVICE = "wifi";
    public static final String TELEPHONY_SERVICE = "phone";
    public static final String CLIPBOARD_SERVICE = "clipboard";
    public static final String NOTIFICATION_SERVICE = "notification";
    public static final String ALARM_SERVICE = "alarm";
    public static final String POWER_SERVICE = "power";
    public static final String KEYGUARD_SERVICE = "keyguard";
    public static final String WINDOW_SERVICE = "window";
    public static final String LAYOUT_INFLATER_SERVICE = "layout_inflater";

    public abstract AssetManager getAssets();
    public abstract Resources getResources();
    public abstract PackageManager getPackageManager();
    public abstract String getPackageName();
    public abstract File getFilesDir();
    public abstract File getCacheDir();
    public abstract File getExternalFilesDir(String type);
    public abstract File getExternalCacheDir();
    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    public Object getSystemService(String name) {
        if (name == null) return null;
        if (name.equals(WINDOW_SERVICE)) return new android.view.WindowManagerImpl();
        if (name.equals(LAYOUT_INFLATER_SERVICE)) return android.view.LayoutInflater.from(this);
        if (name.equals(SENSOR_SERVICE) || name.equals("sensor")) return new SystemSensorManager();
        if (name.equals(AUDIO_SERVICE) || name.equals("audio")) return new android.media.AudioManager();
        if (name.equals(VIBRATOR_SERVICE) || name.equals("vibrator")) return new SystemVibrator();
        if (name.equals(CONNECTIVITY_SERVICE)) return new android.net.ConnectivityManager();
        if (name.equals(WIFI_SERVICE)) return new android.net.wifi.WifiManager();
        if (name.equals(TELEPHONY_SERVICE)) return new android.telephony.TelephonyManager();
        if (name.equals(CLIPBOARD_SERVICE)) return new ClipboardManager();
        if (name.equals(NOTIFICATION_SERVICE)) return new android.app.NotificationManager();
        if (name.equals(ALARM_SERVICE)) return new android.app.AlarmManager();
        if (name.equals(POWER_SERVICE)) return new android.os.PowerManager();
        if (name.equals(KEYGUARD_SERVICE)) return new android.app.KeyguardManager();
        return null;
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
