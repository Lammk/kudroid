package android.content;

import android.content.pm.PackageManager;
import android.content.pm.SystemPackageManager;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.os.Bundle;
import java.io.File;

public class ContextWrapper extends Context {
    Context mBase;

    public ContextWrapper(Context base) {
        mBase = base;
    }
    protected void attachBaseContext(Context base) {
        if (mBase != null) throw new IllegalStateException("Base context already set");
        mBase = base;
    }
    public Context getBaseContext() { return mBase; }
    public AssetManager getAssets() { return mBase != null ? mBase.getAssets() : new AssetManager(); }
    public Resources getResources() { return mBase != null ? mBase.getResources() : new Resources(); }
    public PackageManager getPackageManager() { return mBase != null ? mBase.getPackageManager() : new SystemPackageManager(); }
    public String getPackageName() { return mBase != null ? mBase.getPackageName() : "com.kudroid.app"; }
    public File getFilesDir() { return mBase != null ? mBase.getFilesDir() : new File("/data/data/com.kudroid.app/files"); }
    public File getCacheDir() { return mBase != null ? mBase.getCacheDir() : new File("/data/data/com.kudroid.app/cache"); }
    public File getExternalFilesDir(String type) { return mBase != null ? mBase.getExternalFilesDir(type) : new File("/sdcard/Android/data/com.kudroid.app/files"); }
    public File getExternalCacheDir() { return mBase != null ? mBase.getExternalCacheDir() : new File("/sdcard/Android/data/com.kudroid.app/cache"); }
    public SharedPreferences getSharedPreferences(String name, int mode) { return mBase != null ? mBase.getSharedPreferences(name, mode) : new SharedPreferencesImpl(name); }
    public Object getSystemService(String name) { return mBase != null ? mBase.getSystemService(name) : super.getSystemService(name); }
    public int checkSelfPermission(String permission) { return mBase != null ? mBase.checkSelfPermission(permission) : PackageManager.PERMISSION_GRANTED; }
    public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) { return mBase != null ? mBase.registerReceiver(receiver, filter) : null; }
    public void unregisterReceiver(BroadcastReceiver receiver) { if (mBase != null) mBase.unregisterReceiver(receiver); }
    public void sendBroadcast(Intent intent) { if (mBase != null) mBase.sendBroadcast(intent); }
    public void startActivity(Intent intent) { if (mBase != null) mBase.startActivity(intent); }
    public void startActivity(Intent intent, Bundle options) { if (mBase != null) mBase.startActivity(intent, options); }
    public Context getApplicationContext() { return mBase != null ? mBase.getApplicationContext() : this; }
}
