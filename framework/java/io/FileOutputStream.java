package java.io;

public class FileOutputStream extends OutputStream {
    private String path;
    private FileDescriptor fd;

    public FileOutputStream(String name) throws FileNotFoundException {
        this.path = name;
        this.fd = new FileDescriptor();
    }
    public FileOutputStream(String name, boolean append) throws FileNotFoundException {
        this.path = name;
        this.fd = new FileDescriptor();
    }
    public FileOutputStream(File file) throws FileNotFoundException {
        this(file != null ? file.getPath() : null);
    }
    public FileOutputStream(File file, boolean append) throws FileNotFoundException {
        this(file != null ? file.getPath() : null, append);
    }
    public FileOutputStream(FileDescriptor fdObj) {
        this.fd = fdObj;
    }
    public void write(int b) throws IOException {}
    public void write(byte[] b, int off, int len) throws IOException {}
    public final FileDescriptor getFD() throws IOException { return fd; }
    public void close() throws IOException {}
}
