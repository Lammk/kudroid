package java.util.logging;

import java.util.Enumeration;
import java.util.HashMap;
import java.util.Map;
import java.util.Collections;

public class LogManager {
    private static final LogManager manager = new LogManager();
    private final Map<String, Logger> loggers = new HashMap<String, Logger>();

    protected LogManager() {
        Logger root = new Logger("", null);
        root.setLevel(Level.INFO);
        loggers.put("", root);
    }

    public static LogManager getLogManager() {
        return manager;
    }

    public synchronized boolean addLogger(Logger logger) {
        String name = logger.getName();
        if (loggers.containsKey(name)) return false;
        loggers.put(name, logger);
        return true;
    }

    public synchronized Logger getLogger(String name) {
        return loggers.get(name);
    }

    public Enumeration<String> getLoggerNames() {
        return Collections.enumeration(loggers.keySet());
    }

    public void readConfiguration() throws SecurityException {}
    public void reset() throws SecurityException {}
}
