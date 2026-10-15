package java.util.zip;

import java.io.InputStream;
import java.io.IOException;

public class ZipInputStream extends InflaterInputStream {
    public ZipInputStream(InputStream in) {
        super(in, new Inflater(true));
    }
    public ZipEntry getNextEntry() throws IOException {
        return null;
    }
    public void closeEntry() throws IOException {}
}
