package java.util;

import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.io.Reader;
import java.io.Writer;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;

public class Properties extends Hashtable<Object, Object> {
    private static final long serialVersionUID = 4112578634029874840L;
    protected Properties defaults;

    public Properties() {
        this(null);
    }

    public Properties(Properties defaults) {
        this.defaults = defaults;
    }

    public synchronized Object setProperty(String key, String value) {
        return put(key, value);
    }

    public String getProperty(String key) {
        Object oval = super.get(key);
        String sval = (oval instanceof String) ? (String)oval : null;
        return ((sval == null) && (defaults != null)) ? defaults.getProperty(key) : sval;
    }

    public String getProperty(String key, String defaultValue) {
        String val = getProperty(key);
        return (val == null) ? defaultValue : val;
    }

    public Enumeration<?> propertyNames() {
        Hashtable<String,Object> h = new Hashtable<String,Object>();
        enumerate(h);
        return Collections.enumeration(h.keySet());
    }

    public Set<String> stringPropertyNames() {
        Hashtable<String, String> h = new Hashtable<String, String>();
        enumerateStringProperties(h);
        return h.keySet();
    }

    private synchronized void enumerate(Hashtable<String,Object> h) {
        if (defaults != null) {
            defaults.enumerate(h);
        }
        for (Map.Entry<Object, Object> e : entrySet()) {
            if (e.getKey() instanceof String) {
                h.put((String)e.getKey(), e.getValue());
            }
        }
    }

    private synchronized void enumerateStringProperties(Hashtable<String, String> h) {
        if (defaults != null) {
            defaults.enumerateStringProperties(h);
        }
        for (Map.Entry<Object, Object> e : entrySet()) {
            Object k = e.getKey();
            Object v = e.getValue();
            if (k instanceof String && v instanceof String) {
                h.put((String) k, (String) v);
            }
        }
    }

    public synchronized void load(Reader reader) throws IOException {
        BufferedReader br = (reader instanceof BufferedReader) ? (BufferedReader)reader : new BufferedReader(reader);
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#") || line.startsWith("!")) continue;
            int eq = line.indexOf('=');
            int col = line.indexOf(':');
            int sep = -1;
            if (eq >= 0 && col >= 0) sep = Math.min(eq, col);
            else if (eq >= 0) sep = eq;
            else sep = col;

            if (sep >= 0) {
                String key = line.substring(0, sep).trim();
                String val = line.substring(sep + 1).trim();
                put(key, val);
            } else {
                put(line, "");
            }
        }
    }

    public synchronized void load(InputStream inStream) throws IOException {
        load(new InputStreamReader(inStream, "ISO-8859-1"));
    }

    public void store(Writer writer, String comments) throws IOException {
        BufferedWriter bw = (writer instanceof BufferedWriter) ? (BufferedWriter)writer : new BufferedWriter(writer);
        if (comments != null) {
            bw.write("#" + comments);
            bw.newLine();
        }
        bw.write("#" + new Date().toString());
        bw.newLine();
        for (Map.Entry<Object, Object> e : entrySet()) {
            bw.write(e.getKey() + "=" + e.getValue());
            bw.newLine();
        }
        bw.flush();
    }

    public void store(OutputStream out, String comments) throws IOException {
        store(new OutputStreamWriter(out, "ISO-8859-1"), comments);
    }
}
