package java.util.logging;

public abstract class Formatter {
    protected Formatter() {}
    public abstract String format(LogRecord record);
    public String getHead(Handler h) { return ""; }
    public String getTail(Handler h) { return ""; }
    public String formatMessage(LogRecord record) {
        String format = record.getMessage();
        Object[] params = record.getParameters();
        if (params == null || params.length == 0) return format;
        try {
            return String.format(format, params);
        } catch (Exception e) {
            return format;
        }
    }
}
