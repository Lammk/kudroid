package java.io;

public class FileInputStream extends InputStream {
    private String path;
    private FileDescriptor fd;

    public FileInputStream(String name) throws FileNotFoundException {
        this.path = name;
        this.fd = new FileDescriptor();
    }
    public FileInputStream(File file) throws FileNotFoundException {
        this(file != null ? file.getPath() : null);
    }
    public FileInputStream(FileDescriptor fdObj) {
        this.fd = fdObj;
    }
    public int read() throws IOException { return -1; }
    public int read(byte[] b, int off, int len) throws IOException { return -1; }
    public final FileDescriptor getFD() throws IOException { return fd; }
    public void close() throws IOException {}
}
