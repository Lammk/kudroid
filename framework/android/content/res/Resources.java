package android.content.res;

/**
 * minimal android.content.res.resources implementation.
 *
 * provides access to application resources (string, size, color). for
 * kudroid minimal framework, most lookups return default values.
 */
public class Resources {
    public Resources() {
    }

    /**
     * returns a string resource. currently returns empty string.
     */
    public String getString(int id) {
        return "";
    }

    /**
     * returns a string resource with format arguments.
     */
    public String getString(int id, Object... formatArgs) {
        return "";
    }

    /**
     * returns a color resource. currently returns 0.
     */
    public int getColor(int id) {
        return 0;
    }

    /**
     * returns a resource size in pixels. currently returns 0.
     */
    public float getDimension(int id) {
        return 0.0f;
    }

    /**
     * returns an integer resource. currently returns 0.
     */
    public int getInteger(int id) {
        return 0;
    }

    /**
     * returns a boolean resource. currently returns false.
     */
    public boolean getBoolean(int id) {
        return false;
    }

    /**
     * returns the display metrics.
     */
    public android.util.DisplayMetrics getDisplayMetrics() {
        return new android.util.DisplayMetrics();
    }

    /**
     * returns configuration.
     */
    public android.content.res.Configuration getConfiguration() {
        return new android.content.res.Configuration();
    }

    /**
     * returns asset manager.
     */
    public AssetManager getAssets() {
        return new AssetManager();
    }

    public static class Theme {
        public Theme() {}
    }

}
