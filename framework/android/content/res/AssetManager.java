package android.content.res;

/**
 * Minimal android.content.res.AssetManager implementation.
 *
 * Provides access to the app's bundled assets. For KuDroid's minimal framework,
 * this is a stub that returns null/empty for asset lookups.
 */
public final class AssetManager {
    public AssetManager() {
    }

    /**
     * Open an asset file. Returns null (not found) for now.
     */
    public java.io.InputStream open(String fileName) throws java.io.IOException {
        throw new java.io.FileNotFoundException("Asset not found: " + fileName);
    }

    /**
     * Open an asset file with access mode. Returns null for now.
     */
    public java.io.InputStream open(String fileName, int accessMode) throws java.io.IOException {
        return open(fileName);
    }

    /**
     * List the assets in a directory. Returns empty array for now.
     */
    public String[] list(String path) {
        return new String[0];
    }
}
