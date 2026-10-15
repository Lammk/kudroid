package java.util.logging;

public class XMLFormatter extends Formatter {
    public String format(LogRecord record) {
        return "<record><message>" + formatMessage(record) + "</message></record>\n";
    }
}
