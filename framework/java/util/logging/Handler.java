package java.util.logging;

public abstract class Handler {
    private Level level = Level.ALL;
    private Formatter formatter;

    protected Handler() {}
    public abstract void publish(LogRecord record);
    public abstract void flush();
    public abstract void close() throws SecurityException;
    public void setLevel(Level newLevel) { this.level = newLevel; }
    public Level getLevel() { return level; }
    public void setFormatter(Formatter newFormatter) { this.formatter = newFormatter; }
    public Formatter getFormatter() { return formatter; }
    public boolean isLoggable(LogRecord record) {
        if (record == null) return false;
        return record.getLevel().intValue() >= level.intValue();
    }
}
