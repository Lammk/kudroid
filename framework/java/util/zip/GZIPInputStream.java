package java.util.zip;

import java.io.InputStream;
import java.io.IOException;

public class GZIPInputStream extends InflaterInputStream {
    public static final int GZIP_MAGIC = 0x8b1f;

    public GZIPInputStream(InputStream in, int size) throws IOException {
        super(in, new Inflater(true), size);
    }
    public GZIPInputStream(InputStream in) throws IOException {
        this(in, 512);
    }
}
