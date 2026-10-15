package java.util.logging;

import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;

public class StreamHandler extends Handler {
    private OutputStream out;
    private Writer writer;

    public StreamHandler() {}
    public StreamHandler(OutputStream out, Formatter formatter) {
        setFormatter(formatter);
        setOutputStream(out);
    }

    protected void setOutputStream(OutputStream out) {
        if (out == null) throw new NullPointerException();
        this.out = out;
        this.writer = new OutputStreamWriter(out);
    }

    public synchronized void publish(LogRecord record) {
        if (!isLoggable(record)) return;
        String msg;
        try {
            msg = getFormatter().format(record);
        } catch (Exception ex) {
            return;
        }
        try {
            if (writer != null) {
                writer.write(msg);
                writer.flush();
            }
        } catch (Exception ex) {}
    }

    public boolean isLoggable(LogRecord record) {
        if (writer == null || record == null) return false;
        return super.isLoggable(record);
    }

    public synchronized void flush() {
        if (writer != null) {
            try { writer.flush(); } catch (Exception ex) {}
        }
    }

    public synchronized void close() throws SecurityException {
        flush();
        if (writer != null) {
            try { writer.close(); } catch (Exception ex) {}
            writer = null;
            out = null;
        }
    }
}
