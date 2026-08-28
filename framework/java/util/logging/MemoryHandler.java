package java.util.logging;

public class MemoryHandler extends Handler {
    public MemoryHandler() {}
    public MemoryHandler(Handler target, int size, Level pushLevel) {}
    public void publish(LogRecord record) {}
    public void flush() {}
    public void close() throws SecurityException {}
    public void push() {}
}
