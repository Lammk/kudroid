package java.io;

public class ObjectOutputStream extends OutputStream {
    public ObjectOutputStream(OutputStream out) throws IOException {}
    public ObjectOutputStream() throws IOException, SecurityException {}
    public final void writeObject(Object obj) throws IOException {}
    public void write(int b) throws IOException {}
    public void writeInt(int v) throws IOException {}
    public void defaultWriteObject() throws IOException {}
    public static abstract class PutField {
        public abstract void put(String name, boolean val);
        public abstract void put(String name, byte val);
        public abstract void put(String name, char val);
        public abstract void put(String name, short val);
        public abstract void put(String name, int val);
        public abstract void put(String name, long val);
        public abstract void put(String name, float val);
        public abstract void put(String name, double val);
        public abstract void put(String name, Object val);
    }
    public PutField putFields() throws IOException { return null; }
    public void writeFields() throws IOException {}
}
