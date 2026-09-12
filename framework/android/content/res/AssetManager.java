package android.content.res;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.io.IOException;

import android.os.ParcelFileDescriptor;

public final class AssetManager implements AutoCloseable {
    private static String sAssetsDir;

    public AssetManager() {}

    private static synchronized String assetsDir() {
        if (sAssetsDir == null) {
            sAssetsDir = nativeGetAssetsDir();
            if (sAssetsDir == null) sAssetsDir = "";
        }
        return sAssetsDir;
    }

    private static native String nativeGetAssetsDir();

    // Shim extension: [String path, Long start, Long length] for an asset that is only
    // inside base.apk — the native side answers with the backing file it extracted or
    // mapped, or null when the asset does not exist.
    private static native Object[] nativeResolveAsset(String fileName);

    // Shim extension: loose-assets root backing jar:file:// URLs.
    public static String getAssetsDir() {
        return assetsDir();
    }

    private static File assetFile(String fileName) {
        String name = fileName;
        while (name.startsWith("/")) name = name.substring(1);
        return new File(assetsDir() + "/" + name);
    }

    public InputStream open(String fileName) throws IOException {
        return open(fileName, 0);
    }
    public InputStream open(String fileName, int accessMode) throws IOException {
        File f = assetFile(fileName);
        if (!f.isFile()) throw new FileNotFoundException(fileName);
        return new FileInputStream(f);
    }
    public AssetFileDescriptor openFd(String fileName) throws IOException {
        File f = assetFile(fileName);
        // Loose file first: the common case, and the one with a direct fd.
        if (f.isFile()) {
            FileInputStream in = new FileInputStream(f);
            return new AssetFileDescriptor(new ParcelFileDescriptor(in.getFD()), 0, f.length());
        }
        // Inside base.apk: the native resolver extracted (deflated) or maps (stored)
        // the entry; open the file it names at the offset it reports.
        Object[] resolved = nativeResolveAsset(fileName);
        if (resolved != null && resolved.length == 3 &&
                resolved[0] instanceof String && resolved[1] instanceof Long &&
                resolved[2] instanceof Long) {
            File backing = new File((String) resolved[0]);
            long start = (Long) resolved[1];
            long length = (Long) resolved[2];
            if (backing.isFile()) {
                FileInputStream in = new FileInputStream(backing);
                return new AssetFileDescriptor(new ParcelFileDescriptor(in.getFD()),
                                               start, length);
            }
        }
        throw new FileNotFoundException(fileName);
    }
    public String[] list(String path) throws IOException {
        String[] names = new File(assetsDir() + "/" + path).list();
        return names != null ? names : new String[0];
    }
    public String[] getLocales() {
        return new String[]{"en_US"};
    }
    public void close() {}
}
