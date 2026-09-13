package java.net;

import android.content.res.AssetManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;

// jar:<inner-url>!/<entry>. Loose files first; anything else inside base.apk
// streams through AssetManager's resolver (stored entries as a bounded APK
// slice, deflated ones from the extraction cache). Without the fallback every
// bundle/resource/catalog fetch is a FileNotFoundException and Addressables
// boots contentless.
class JarURLConnection extends URLConnection {
    private final String entry;
    private final File file;
    private long entryLength = -1;

    JarURLConnection(URL url) throws MalformedURLException {
        super(url);
        String spec = url.getFile();
        int sep = spec.indexOf("!/");
        if (sep < 0) throw new MalformedURLException("no !/ in " + url);
        String e = spec.substring(sep + 2);
        String root = AssetManager.getAssetsDir();
        if (e.startsWith("assets/")) e = e.substring("assets/".length());
        while (e.startsWith("/")) e = e.substring(1);
        if (e.isEmpty() || root.isEmpty()) {
            throw new MalformedURLException("unresolvable " + url);
        }
        this.entry = e;
        this.file = new File(root + "/" + e);
    }

    public void connect() throws IOException {
        if (file.isFile()) {
            entryLength = file.length();
            connected = true;
            return;
        }
        // Probe the APK backing without consuming: openAssetStream throws
        // FileNotFoundException when the entry is in neither place.
        InputStream probe = AssetManager.openAssetStream(entry);
        try {
            if (probe instanceof AssetManager.BoundedInputStream) {
                entryLength = ((AssetManager.BoundedInputStream) probe).getLength();
            }
        } finally {
            try {
                probe.close();
            } catch (IOException ignored) {}
        }
        connected = true;
    }

    public InputStream getInputStream() throws IOException {
        connect();
        if (file.isFile()) return new FileInputStream(file);
        return AssetManager.openAssetStream(entry);
    }

    public int getContentLength() {
        long len = getContentLengthLong();
        return len > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) len;
    }

    public long getContentLengthLong() {
        if (entryLength >= 0) return entryLength;
        if (file.isFile()) return file.length();
        return -1;
    }
}
