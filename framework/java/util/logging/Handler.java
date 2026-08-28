package java.util.logging;

public abstract class Handler {
    private Formatter formatter = new SimpleFormatter();
    private Filter filter;
    private Level level = Level.ALL;

    protected Handler() {}

    public abstract void publish(LogRecord record);
    public abstract void flush();
    public abstract void close() throws SecurityException;

    public void setFormatter(Formatter newFormatter) {
        if (newFormatter == null) throw new NullPointerException();
        this.formatter = newFormatter;
    }
    public Formatter getFormatter() { return formatter; }

    public void setFilter(Filter newFilter) { this.filter = newFilter; }
    public Filter getFilter() { return filter; }

    public void setLevel(Level newLevel) {
        if (newLevel == null) throw new NullPointerException();
        this.level = newLevel;
    }
    public Level getLevel() { return level; }

    public boolean isLoggable(LogRecord record) {
        if (record == null) return false;
        int levelValue = getLevel().intValue();
        if (record.getLevel().intValue() < levelValue || levelValue == Level.OFF.intValue()) {
            return false;
        }
        return filter == null || filter.isLoggable(record);
    }
}
