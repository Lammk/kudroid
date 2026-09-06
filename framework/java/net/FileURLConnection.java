package java.net;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;

class FileURLConnection extends URLConnection {
    private final File file;

    FileURLConnection(URL url) throws MalformedURLException {
        super(url);
        String path = url.getFile();
        while (path.startsWith("//")) path = path.substring(1);
        if (path.isEmpty()) throw new MalformedURLException("empty file URL " + url);
        this.file = new File(path);
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
