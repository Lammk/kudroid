package android.content.res;

/**
 * minimal android.content.res.assetmanager implementation.
 *
 * provides access to the application's packaged assets. for kudroid minimal framework,
 * this is a simulation of returning null/empty for property lookups.
 */
public final class AssetManager {
    public AssetManager() {
    }

    /**
     * opens a content file. currently returns null (not found).
     */
    public java.io.InputStream open(String fileName) throws java.io.IOException {
        throw new java.io.FileNotFoundException("Asset not found: " + fileName);
    }

    /**
     * opens a content file with accessible mode. currently returns null.
     */
    public java.io.InputStream open(String fileName, int accessMode) throws java.io.IOException {
        return open(fileName);
    }

    /**
     * lists assets in a folder. currently returns empty array.
     */
    public String[] list(String path) {
        return new String[0];
    }
}
