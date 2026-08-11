package android.content.res;

/**
 * Minimal android.content.res.Resources implementation.
 *
 * Provides access to app resources (strings, dimensions, colors). For
 * KuDroid's minimal framework, most lookups return defaults.
 */
public class Resources {
    public Resources() {
    }

    /**
     * Return a string resource. Returns empty string for now.
     */
    public String getString(int id) {
        return "";
    }

    /**
     * Return a string resource with format args.
     */
    public String getString(int id, Object... formatArgs) {
        return "";
    }

    /**
     * Return a color resource. Returns 0 for now.
     */
    public int getColor(int id) {
        return 0;
    }

    /**
     * Return a dimension resource in pixels. Returns 0 for now.
     */
    public float getDimension(int id) {
        return 0.0f;
    }

    /**
     * Return an integer resource. Returns 0 for now.
     */
    public int getInteger(int id) {
        return 0;
    }

    /**
     * Return a boolean resource. Returns false for now.
     */
    public boolean getBoolean(int id) {
        return false;
    }

    /**
     * Return the display metrics.
     */
    public android.util.DisplayMetrics getDisplayMetrics() {
        return new android.util.DisplayMetrics();
    }

    /**
     * Return the configuration.
     */
    public android.content.res.Configuration getConfiguration() {
        return new android.content.res.Configuration();
    }

    /**
     * Return the asset manager.
     */
    public AssetManager getAssets() {
        return new AssetManager();
    }
}
