package android.content.res;

import java.io.InputStream;
import java.io.ByteArrayInputStream;

public class Resources {
    private final DisplayMetrics mMetrics = new DisplayMetrics();
    private final Configuration mConfiguration = new Configuration();
    private final AssetManager mAssets = new AssetManager();

    public Resources() {}
    public Resources(AssetManager assets, DisplayMetrics metrics, Configuration config) {
        if (metrics != null) mMetrics.setTo(metrics);
        if (config != null) mConfiguration.setTo(config);
    }
    public static Resources getSystem() { return new Resources(); }
    public DisplayMetrics getDisplayMetrics() { return mMetrics; }
    public Configuration getConfiguration() { return mConfiguration; }
    public AssetManager getAssets() { return mAssets; }
    public String getString(int id) { return ""; }
    public String getString(int id, Object... formatArgs) { return ""; }
    public String[] getStringArray(int id) { return new String[0]; }
    public CharSequence getText(int id) { return ""; }
    public int getIdentifier(String name, String defType, String defPackage) { return 0; }
    public int getColor(int id) { return 0xFF000000; }
    public float getDimension(int id) { return 0.0f; }
    public int getDimensionPixelSize(int id) { return 0; }
    public int getDimensionPixelOffset(int id) { return 0; }
    public boolean getBoolean(int id) { return false; }
    public int getInteger(int id) { return 0; }
    public InputStream openRawResource(int id) { return new ByteArrayInputStream(new byte[0]); }
    public static class NotFoundException extends RuntimeException {
        public NotFoundException() { super(); }
        public NotFoundException(String name) { super(name); }
    }
}
