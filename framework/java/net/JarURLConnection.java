package java.net;

import android.content.res.AssetManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;

// jar:<inner-url>!/<entry>. APK entries under assets/ are extracted loose, so
// jar:file://.../base.apk!/assets/aa/x reads the loose file instead of the zip.
class JarURLConnection extends URLConnection {
    private final File file;

    JarURLConnection(URL url) throws MalformedURLException {
        super(url);
        String spec = url.getFile();
        int sep = spec.indexOf("!/");
        if (sep < 0) throw new MalformedURLException("no !/ in " + url);
        String entry = spec.substring(sep + 2);
        String root = AssetManager.getAssetsDir();
        if (entry.startsWith("assets/")) entry = entry.substring("assets/".length());
        while (entry.startsWith("/")) entry = entry.substring(1);
        if (entry.isEmpty() || root.isEmpty()) {
            throw new MalformedURLException("unresolvable " + url);
        }
        this.file = new File(root + "/" + entry);
    }

    public void connect() throws IOException {
        if (!file.isFile()) throw new FileNotFoundException(url.toString());
        connected = true;
    }

    public InputStream getInputStream() throws IOException {
        connect();
        return new FileInputStream(file);
    }

    public int getContentLength() {
        long len = file.length();
        return len > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) len;
    }

    public long getContentLengthLong() {
        return file.length();
    }
}
