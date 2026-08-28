package java.util.logging;

import java.util.ArrayList;
import java.util.List;

public class Logger {
    public static final String GLOBAL_LOGGER_NAME = "global";
    private static final Logger global = new Logger(GLOBAL_LOGGER_NAME, null);

    private final String name;
    private Level level = Level.INFO;
    private final List<Handler> handlers = new ArrayList<Handler>();
    private boolean useParentHandlers = true;

    protected Logger(String name, String resourceBundleName) {
        this.name = name;
    }

    public static Logger getGlobal() {
        return global;
    }

    public static Logger getLogger(String name) {
        LogManager manager = LogManager.getLogManager();
        Logger result = manager.getLogger(name);
        if (result == null) {
            result = new Logger(name, null);
            manager.addLogger(result);
        }
        return result;
    }

    public static Logger getLogger(String name, String resourceBundleName) {
        return getLogger(name);
    }

    public static Logger getAnonymousLogger() {
        return new Logger(null, null);
    }

    public static Logger getAnonymousLogger(String resourceBundleName) {
        return new Logger(null, null);
    }

    public String getName() { return name; }
    public Level getLevel() { return level; }
    public void setLevel(Level newLevel) { this.level = newLevel; }

    public void addHandler(Handler handler) {
        if (handler != null) handlers.add(handler);
    }
    public void removeHandler(Handler handler) {
        handlers.remove(handler);
    }
    public Handler[] getHandlers() {
        return handlers.toArray(new Handler[handlers.size()]);
    }
    public void setUseParentHandlers(boolean useParentHandlers) {
        this.useParentHandlers = useParentHandlers;
    }
    public boolean getUseParentHandlers() {
        return useParentHandlers;
    }

    public boolean isLoggable(Level level) {
        if (level == null) return false;
        int levelVal = (this.level != null) ? this.level.intValue() : Level.INFO.intValue();
        return level.intValue() >= levelVal && levelVal != Level.OFF.intValue();
    }

    public void log(LogRecord record) {
        if (!isLoggable(record.getLevel())) return;
        android.util.Log.i(name != null ? name : "Logger", record.getMessage());
        for (Handler h : handlers) {
            h.publish(record);
        }
    }

    public void log(Level level, String msg) {
        if (isLoggable(level)) {
            LogRecord lr = new LogRecord(level, msg);
            lr.setLoggerName(name);
            log(lr);
        }
    }

    public void log(Level level, String msg, Object param1) {
        if (isLoggable(level)) {
            LogRecord lr = new LogRecord(level, msg);
            lr.setLoggerName(name);
            lr.setParameters(new Object[]{param1});
            log(lr);
        }
    }

    public void log(Level level, String msg, Object[] params) {
        if (isLoggable(level)) {
            LogRecord lr = new LogRecord(level, msg);
            lr.setLoggerName(name);
            lr.setParameters(params);
            log(lr);
        }
    }

    public void log(Level level, String msg, Throwable thrown) {
        if (isLoggable(level)) {
            LogRecord lr = new LogRecord(level, msg);
            lr.setLoggerName(name);
            lr.setThrown(thrown);
            log(lr);
        }
    }

    public void severe(String msg) { log(Level.SEVERE, msg); }
    public void warning(String msg) { log(Level.WARNING, msg); }
    public void info(String msg) { log(Level.INFO, msg); }
    public void config(String msg) { log(Level.CONFIG, msg); }
    public void fine(String msg) { log(Level.FINE, msg); }
    public void finer(String msg) { log(Level.FINER, msg); }
    public void finest(String msg) { log(Level.FINEST, msg); }

    public void entering(String sourceClass, String sourceMethod) {
        log(Level.FINER, "ENTRY {0} {1}", new Object[]{sourceClass, sourceMethod});
    }
    public void entering(String sourceClass, String sourceMethod, Object param1) {
        log(Level.FINER, "ENTRY {0} {1} {2}", new Object[]{sourceClass, sourceMethod, param1});
    }
    public void entering(String sourceClass, String sourceMethod, Object[] params) {
        log(Level.FINER, "ENTRY", params);
    }
    public void exiting(String sourceClass, String sourceMethod) {
        log(Level.FINER, "RETURN {0} {1}", new Object[]{sourceClass, sourceMethod});
    }
    public void exiting(String sourceClass, String sourceMethod, Object result) {
        log(Level.FINER, "RETURN {0} {1} {2}", new Object[]{sourceClass, sourceMethod, result});
    }
}
