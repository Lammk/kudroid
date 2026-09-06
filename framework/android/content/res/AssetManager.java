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
        if (!f.isFile()) throw new FileNotFoundException(fileName);
        FileInputStream in = new FileInputStream(f);
        return new AssetFileDescriptor(new ParcelFileDescriptor(in.getFD()), 0, f.length());
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
