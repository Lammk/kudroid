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

    /**
     * Whether records also go to the parent's handlers.
     *
     * Libraries call this right after creating a logger, to stop their output being
     * duplicated by the root handler. It was auto-stubbed, so the call silently did
     * nothing; honoured here because a library that asked for no parent handling and
     * got it anyway prints everything twice.
     */
    public void setUseParentHandlers(boolean useParentHandlers) {
        this.useParentHandlers = useParentHandlers;
    }

    public boolean getUseParentHandlers() { return useParentHandlers; }

    /**
     * The parent logger.
     *
     * KuDroid keeps no logger hierarchy — getLogger() hands out a fresh instance —
     * so every logger's parent is the global one, except the global logger itself
     * which has none. Returning the global logger rather than null matters because
     * callers walk the chain and a null at the first step ends the walk immediately.
     */
    public Logger getParent() {
        return this == global ? null : global;
    }

    public void setParent(Logger parent) {}

    public void setFilter(Filter newFilter) { this.filter = newFilter; }

    public Filter getFilter() { return filter; }

    public void logp(Level level, String sourceClass, String sourceMethod, String msg) {
        log(level, msg);
    }

    public void logp(Level level, String sourceClass, String sourceMethod, String msg,
                     Throwable thrown) {
        log(level, msg, thrown);
    }

    public void entering(String sourceClass, String sourceMethod) {
        log(Level.FINER, "ENTRY " + sourceClass + "." + sourceMethod);
    }

    public void exiting(String sourceClass, String sourceMethod) {
        log(Level.FINER, "RETURN " + sourceClass + "." + sourceMethod);
    }

    public void throwing(String sourceClass, String sourceMethod, Throwable thrown) {
        log(Level.FINER, "THROW " + sourceClass + "." + sourceMethod, thrown);
    }

    public void log(LogRecord record) {
        if (record == null) return;
        log(record.getLevel(), record.getMessage());
    }

    private boolean useParentHandlers = true;
    private Filter filter;
}
