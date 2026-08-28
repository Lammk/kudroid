package java.util.logging;

public class ConsoleHandler extends StreamHandler {
    public ConsoleHandler() {
        setOutputStream(System.err);
    }
    public void publish(LogRecord record) {
        if (!isLoggable(record)) return;
        String msg = getFormatter().format(record);
        System.err.print(msg);
    }
}
