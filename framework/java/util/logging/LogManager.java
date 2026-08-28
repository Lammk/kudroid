package java.util.logging;

import java.util.Enumeration;
import java.util.Collections;

public class LogManager {
    private static final LogManager instance = new LogManager();

    protected LogManager() {}
    public static LogManager getLogManager() { return instance; }
    public boolean addLogger(Logger logger) { return true; }
    public Logger getLogger(String name) { return Logger.getLogger(name); }
    public Enumeration<String> getLoggerNames() { return Collections.emptyEnumeration(); }
    public void reset() {}
}
