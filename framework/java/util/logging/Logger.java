package java.util.logging;

import java.util.concurrent.CopyOnWriteArrayList;
import java.util.List;

public class Logger {
    public static final String GLOBAL_LOGGER_NAME = "global";
    private static final Logger global = new Logger(GLOBAL_LOGGER_NAME, null);
    private String name;
    private Level level = Level.INFO;
    private final List<Handler> handlers = new CopyOnWriteArrayList<Handler>();

    protected Logger(String name, String resourceBundleName) {
        this.name = name;
    }
    public static Logger getLogger(String name) {
        return new Logger(name, null);
    }
    public static Logger getLogger(String name, String resourceBundleName) {
        return new Logger(name, resourceBundleName);
    }
    public static Logger getGlobal() { return global; }
    public String getName() { return name; }
    public void setLevel(Level newLevel) { this.level = newLevel; }
    public Level getLevel() { return level; }
    public boolean isLoggable(Level level) {
        return level != null && level.intValue() >= this.level.intValue();
    }
    public void log(Level level, String msg) {
        if (!isLoggable(level)) return;
        System.out.println("[" + (name != null ? name : "Logger") + "][" + level + "] " + msg);
    }
    public void log(Level level, String msg, Object param1) {
        log(level, msg);
    }
    public void log(Level level, String msg, Object[] params) {
        log(level, msg);
    }
    public void log(Level level, String msg, Throwable thrown) {
        log(level, msg + (thrown != null ? ": " + thrown.getMessage() : ""));
    }
    public void severe(String msg) { log(Level.SEVERE, msg); }
    public void warning(String msg) { log(Level.WARNING, msg); }
    public void info(String msg) { log(Level.INFO, msg); }
    public void config(String msg) { log(Level.CONFIG, msg); }
    public void fine(String msg) { log(Level.FINE, msg); }
    public void finer(String msg) { log(Level.FINER, msg); }
    public void finest(String msg) { log(Level.FINEST, msg); }
    public void addHandler(Handler handler) { if (handler != null) handlers.add(handler); }
    public void removeHandler(Handler handler) { if (handler != null) handlers.remove(handler); }
    public Handler[] getHandlers() { return handlers.toArray(new Handler[0]); }
}
