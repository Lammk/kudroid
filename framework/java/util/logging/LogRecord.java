package java.util.logging;

import java.io.Serializable;

public class LogRecord implements Serializable {
    private static final long serialVersionUID = 5358665894625723451L;

    private Level level;
    private String msg;
    private String loggerName;
    private Throwable thrown;
    private long millis;
    private Object[] parameters;

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
    public Throwable getThrown() { return thrown; }
    public void setThrown(Throwable thrown) { this.thrown = thrown; }
    public long getMillis() { return millis; }
    public void setMillis(long millis) { this.millis = millis; }
    public Object[] getParameters() { return parameters; }
    public void setParameters(Object[] parameters) { this.parameters = parameters; }
}
