package java.util.zip;

import java.io.OutputStream;
import java.io.IOException;

public class ZipOutputStream extends DeflaterOutputStream {
    public ZipOutputStream(OutputStream out) {
        super(out, new Deflater(Deflater.DEFAULT_COMPRESSION, true));
    }
    public void putNextEntry(ZipEntry e) throws IOException {}
    public void closeEntry() throws IOException {}
}
