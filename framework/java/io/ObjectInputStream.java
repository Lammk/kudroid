package java.io;

public class ObjectInputStream extends InputStream {
    public ObjectInputStream(InputStream in) throws IOException {}
    public ObjectInputStream() throws IOException, SecurityException {}
    public final Object readObject() throws IOException, ClassNotFoundException { return null; }
    public int read() throws IOException { return -1; }
    public int readInt() throws IOException { return 0; }
    public void defaultReadObject() throws IOException, ClassNotFoundException {}
    public static abstract class GetField {
        public abstract boolean defaulted(String name) throws IOException;
        public abstract boolean get(String name, boolean val) throws IOException;
        public abstract byte get(String name, byte val) throws IOException;
        public abstract char get(String name, char val) throws IOException;
        public abstract short get(String name, short val) throws IOException;
        public abstract int get(String name, int val) throws IOException;
        public abstract long get(String name, long val) throws IOException;
        public abstract float get(String name, float val) throws IOException;
        public abstract double get(String name, double val) throws IOException;
        public abstract Object get(String name, Object val) throws IOException;
    }
    public GetField readFields() throws IOException, ClassNotFoundException { return null; }
}
