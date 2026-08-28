package java.util.logging;

import java.io.Serializable;

public class LogRecord implements Serializable {
    private Level level;
    private String msg;
    private String loggerName;
    private long millis;
    private Throwable thrown;

    public LogRecord(Level level, String msg) {
        this.level = level;
        this.msg = msg;
        this.millis = System.currentTimeMillis();
    }
    public Level getLevel() { return level; }
    public void setLevel(Level level) { this.level = level; }
    public String getMessage() { return msg; }
    public void setMessage(String message) { this.msg = message; }
    public String getLoggerName() { return loggerName; }
    public void setLoggerName(String name) { this.loggerName = name; }
    public long getMillis() { return millis; }
    public void setMillis(long millis) { this.millis = millis; }
    public Throwable getThrown() { return thrown; }
    public void setThrown(Throwable thrown) { this.thrown = thrown; }
}
