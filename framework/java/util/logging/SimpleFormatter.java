package java.util.logging;

import java.io.PrintWriter;
import java.io.StringWriter;

public class SimpleFormatter extends Formatter {
    public synchronized String format(LogRecord record) {
        StringBuilder sb = new StringBuilder();
        sb.append("[").append(record.getLevel() != null ? record.getLevel().getName() : "INFO").append("] ");
        if (record.getLoggerName() != null) sb.append(record.getLoggerName()).append(": ");
        sb.append(formatMessage(record)).append("\n");
        if (record.getThrown() != null) {
            sb.append(record.getThrown().toString()).append("\n");
        }
        return sb.toString();
    }
}
