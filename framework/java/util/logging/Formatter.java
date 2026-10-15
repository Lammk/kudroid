package java.util.logging;

public abstract class Formatter {
    protected Formatter() {}
    public abstract String format(LogRecord record);
    public String formatMessage(LogRecord record) {
        return record != null ? record.getMessage() : "";
    }
}
