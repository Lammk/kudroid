package android.content;

/**
 * minimal android.content.contextwrapper implementation.
 *
 * a context delegates all calls to another context (base). for
 * kudroid minimal framework, it provides authorization pattern used by
 * activity/application.
 */
public class ContextWrapper extends Context {
    private Context mBase;

    public ContextWrapper(Context base) {
        mBase = base;
    }

    /**
     * set the base context.
     */
    protected void attachBaseContext(Context base) {
        mBase = base;
    }

    /**
     * returns the base context.
     */
    public Context getBaseContext() {
        return mBase;
    }

    @Override
    public Context getApplicationContext() {
        return mBase != null ? mBase.getApplicationContext() : this;
    }

    @Override
    public String getPackageName() {
        return mBase != null ? mBase.getPackageName() : "";
    }

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        return mBase != null ? mBase.getSharedPreferences(name, mode)
                             : new SharedPreferencesImpl();
    }

    @Override
    public Object getSystemService(String name) {
        return mBase != null ? mBase.getSystemService(name) : null;
    }

    @Override
    public String getString(int resId) {
        return mBase != null ? mBase.getString(resId) : "";
    }

    @Override
    public String getString(int resId, Object... formatArgs) {
        return mBase != null ? mBase.getString(resId, formatArgs) : "";
    }

    @Override
    public void startActivity(Intent intent) {
        if (mBase != null) mBase.startActivity(intent);
    }

    @Override
    public android.content.res.AssetManager getAssets() {
        return mBase != null ? mBase.getAssets() : new android.content.res.AssetManager();
    }

    @Override
    public android.content.res.Resources getResources() {
        return mBase != null ? mBase.getResources() : new android.content.res.Resources();
    }

    @Override
    public android.content.ContentResolver getContentResolver() {
        return mBase != null ? mBase.getContentResolver() : new android.content.ContentResolver(this);
    }

    @Override
    public android.os.Looper getMainLooper() {
        return mBase != null ? mBase.getMainLooper() : android.os.Looper.getMainLooper();
    }

    @Override
    public android.content.pm.PackageManager getPackageManager() {
        return mBase != null ? mBase.getPackageManager() : new android.content.pm.PackageManager();
    }

    @Override
    public android.content.pm.ApplicationInfo getApplicationInfo() {
        return mBase != null ? mBase.getApplicationInfo() : new android.content.pm.ApplicationInfo();
    }

    @Override
    public ClassLoader getClassLoader() {
        return mBase != null ? mBase.getClassLoader() : ContextWrapper.class.getClassLoader();
    }

    @Override
    public java.io.File getFilesDir() {
        return mBase != null ? mBase.getFilesDir()
                             : new java.io.File("/data/data/" + getPackageName() + "/files");
    }

    @Override
    public java.io.File getCacheDir() {
        return mBase != null ? mBase.getCacheDir()
                             : new java.io.File("/data/data/" + getPackageName() + "/cache");
    }

    @Override
    public java.io.File getExternalFilesDir(String type) {
        return mBase != null ? mBase.getExternalFilesDir(type)
                             : new java.io.File("/sdcard/Android/data/" + getPackageName() + "/files");
    }

    @Override
    public java.io.File getDatabasePath(String name) {
        return mBase != null ? mBase.getDatabasePath(name)
                             : new java.io.File("/data/data/" + getPackageName() + "/databases/" + name);
    }

    @Override
    public java.io.File getSharedPrefsFile(String name) {
        return mBase != null ? mBase.getSharedPrefsFile(name)
                             : new java.io.File("/data/data/" + getPackageName() + "/shared_prefs/" + name + ".xml");
    }

    @Override
    public java.io.File getObbDir() {
        return mBase != null ? mBase.getObbDir()
                             : new java.io.File("/sdcard/Android/obb/" + getPackageName());
    }
}
