package android.app;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.SystemPackageManager;
import android.content.res.AssetManager;
import android.content.res.Resources;
import java.io.File;

/**
 * The base Context every component is attached to.
 *
 * ContextWrapper falls back to a fixed "com.kudroid.app" package name when it has
 * no base context, so every path derived from it (files dir, cache dir, shared
 * preferences, external storage) pointed at a directory unrelated to the app being
 * run. An app that writes a file and reads it back still worked, but anything
 * comparing getPackageName() against its own identity — license checks, intent
 * targeting, provider authorities, crash reporters — saw the wrong answer.
 *
 * This is the root of the context chain: it takes the package name the manifest
 * declared and derives the standard Android paths from it, so nothing is specific
 * to any one app.
 */
public class ApplicationContext extends Context {

    private final String mPackageName;
    private AssetManager mAssets;
    private Resources mResources;
    private PackageManager mPackageManager;

    public ApplicationContext() {
        this(ActivityThread.getPackageName());
    }

    public ApplicationContext(String packageName) {
        mPackageName = (packageName != null && !packageName.isEmpty())
                ? packageName : "android.app.unknown";
    }

    @Override
    public String getPackageName() {
        return mPackageName;
    }

    @Override
    public String getPackageCodePath() {
        return "/data/app/" + mPackageName + "/base.apk";
    }

    @Override
    public String getPackageResourcePath() {
        return getPackageCodePath();
    }

    @Override
    public AssetManager getAssets() {
        if (mAssets == null) mAssets = new AssetManager();
        return mAssets;
    }

    @Override
    public Resources getResources() {
        if (mResources == null) mResources = new Resources();
        return mResources;
    }

    @Override
    public PackageManager getPackageManager() {
        if (mPackageManager == null) mPackageManager = new SystemPackageManager();
        return mPackageManager;
    }

    @Override
    public File getFilesDir() {
        return ensure(new File("/data/data/" + mPackageName + "/files"));
    }

    @Override
    public File getCacheDir() {
        return ensure(new File("/data/data/" + mPackageName + "/cache"));
    }

    @Override
    public File getExternalFilesDir(String type) {
        String path = "/sdcard/Android/data/" + mPackageName + "/files";
        if (type != null && !type.isEmpty()) path = path + "/" + type;
        return ensure(new File(path));
    }

    @Override
    public File getExternalCacheDir() {
        return ensure(new File("/sdcard/Android/data/" + mPackageName + "/cache"));
    }

    /**
     * Preferences, cached by name.
     *
     * A fresh instance per call is a bug that hides as a persistence problem: two calls
     * return two independent stores, so a value written through one is absent from the
     * other and even a write-then-read within a single run yields the default. Android
     * returns the same instance for a given name, and apps rely on it — they also register
     * change listeners, which only fire if the writer and the listener share the object.
     */
    private static final java.util.HashMap<String, SharedPreferences> sPrefs =
            new java.util.HashMap<String, SharedPreferences>();

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        final String key = name != null ? name : "default";
        synchronized (sPrefs) {
            SharedPreferences existing = sPrefs.get(key);
            if (existing != null) return existing;
            SharedPreferences created = new android.content.SharedPreferencesImpl(
                    key, ensure(new File("/data/data/" + mPackageName + "/shared_prefs")));
            sPrefs.put(key, created);
            return created;
        }
    }

    @Override
    public File getDir(String name, int mode) {
        return ensure(new File("/data/data/" + mPackageName + "/app_" + name));
    }

    @Override
    public File getDatabasePath(String name) {
        ensure(new File("/data/data/" + mPackageName + "/databases"));
        return new File("/data/data/" + mPackageName + "/databases/" + name);
    }

    @Override
    public File getObbDir() {
        return ensure(new File("/sdcard/Android/obb/" + mPackageName));
    }

    @Override
    public File getCodeCacheDir() {
        return ensure(new File("/data/data/" + mPackageName + "/code_cache"));
    }

    @Override
    public File getNoBackupFilesDir() {
        return ensure(new File("/data/data/" + mPackageName + "/no_backup"));
    }

    @Override
    public Context getApplicationContext() {
        Application app = ActivityThread.currentApplication();
        return app != null ? app : this;
    }

    public ClassLoader getClassLoader() {
        return ClassLoader.getSystemClassLoader();
    }

    /**
     * Create the directory if it does not exist.
     *
     * Android guarantees these directories exist before handing them out; code that
     * opens a FileOutputStream straight into getFilesDir() is normal and would fail
     * on a missing parent.
     */
    private static File ensure(File dir) {
        try {
            if (!dir.exists()) dir.mkdirs();
        } catch (Throwable ignored) {}
        return dir;
    }
}
