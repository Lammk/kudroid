package android.content;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * SharedPreferences, persisted to disk.
 * On-disk format matches Android's {@code <map>} XML representation. Existing KuDroid
 * line-format files are read once and migrated on the next successful write.
 */
public class SharedPreferencesImpl implements SharedPreferences {
    private final Map<String, Object> mMap = new HashMap<String, Object>();
    private final String mName;
    private final File mFile;
    private final File mLegacyFile;
    private final Object mDiskWriteLock;
    private final List<OnSharedPreferenceChangeListener> mListeners =
            new ArrayList<OnSharedPreferenceChangeListener>();
    private static final Map<String, Object> sDiskWriteLocks = new HashMap<String, Object>();

    public SharedPreferencesImpl() { this("default", null); }
    public SharedPreferencesImpl(String name) { this(name, null); }

    /**
     * @param directory where to persist, or null for memory only.
     *
     * A null directory is not a silent fallback for an unwritable path — it is for callers
     * that genuinely have no context (the no-arg constructor). A caller that passes a
     * directory expects persistence, so a failure there is logged.
     */
    public SharedPreferencesImpl(String name, File directory) {
        this.mName = name != null ? name : "default";
        this.mFile = directory != null ? new File(directory, this.mName + ".xml") : null;
        this.mLegacyFile = directory != null ? new File(directory, this.mName + ".prefs") : null;
        this.mDiskWriteLock = this.mFile != null ? diskWriteLock(this.mFile) : new Object();
        if (this.mFile != null) {
            load();
        }
    }

    private static Object diskWriteLock(File file) {
        String path = file.getAbsolutePath();
        synchronized (sDiskWriteLocks) {
            Object lock = sDiskWriteLocks.get(path);
            if (lock == null) {
                lock = new Object();
                sDiskWriteLocks.put(path, lock);
            }
            return lock;
        }
    }

    public Map<String, ?> getAll() {
        synchronized (mMap) {
            return new HashMap<String, Object>(mMap);
        }
    }

    public String getString(String key, String defValue) {
        synchronized (mMap) {
            Object v = mMap.get(key);
            return (v instanceof String) ? (String) v : defValue;
        }
    }

    public Set<String> getStringSet(String key, Set<String> defValues) {
        synchronized (mMap) {
            Object v = mMap.get(key);
            if (v instanceof Set) {
                // A copy: Android documents that the returned set must not be modified, and
                // handing out the live one lets a caller corrupt the store silently.
                return new LinkedHashSet<String>((Set<String>) v);
            }
            // defValues is returned as-is, including null — a caller that passed null
            // expects null back rather than an empty set.
            return defValues;
        }
    }

    public int getInt(String key, int defValue) {
        synchronized (mMap) {
            Object v = mMap.get(key);
            return (v instanceof Number) ? ((Number) v).intValue() : defValue;
        }
    }

    public long getLong(String key, long defValue) {
        synchronized (mMap) {
            Object v = mMap.get(key);
            return (v instanceof Number) ? ((Number) v).longValue() : defValue;
        }
    }

    public float getFloat(String key, float defValue) {
        synchronized (mMap) {
            Object v = mMap.get(key);
            return (v instanceof Number) ? ((Number) v).floatValue() : defValue;
        }
    }

    public boolean getBoolean(String key, boolean defValue) {
        synchronized (mMap) {
            Object v = mMap.get(key);
            return (v instanceof Boolean) ? ((Boolean) v).booleanValue() : defValue;
        }
    }

    public boolean contains(String key) {
        synchronized (mMap) {
            return mMap.containsKey(key);
        }
    }

    public Editor edit() { return new EditorImpl(); }

    public void registerOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener l) {
        if (l == null) return;
        synchronized (mListeners) {
            if (!mListeners.contains(l)) mListeners.add(l);
        }
    }

    public void unregisterOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener l) {
        if (l == null) return;
        synchronized (mListeners) {
            mListeners.remove(l);
        }
    }

    private void notifyChanged(List<String> keys) {
        Object[] listeners;
        synchronized (mListeners) {
            if (mListeners.isEmpty()) return;
            listeners = mListeners.toArray();
        }
        // Called outside the locks: a listener commonly reads the preferences back, and
        // holding mMap across the callback would deadlock against its own getters.
        for (int i = 0; i < listeners.length; i++) {
            OnSharedPreferenceChangeListener l = (OnSharedPreferenceChangeListener) listeners[i];
            for (int k = 0; k < keys.size(); k++) {
                try {
                    l.onSharedPreferenceChanged(this, keys.get(k));
                } catch (Throwable t) {
                    android.util.Log.e("SharedPreferences", "listener threw: " + t);
                }
            }
        }
    }

    // Persistence.

    private static String escape(String s) {
        StringBuilder out = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\\') out.append("\\\\");
            else if (c == '\n') out.append("\\n");
            else if (c == '\r') out.append("\\r");
            else if (c == '=') out.append("\\e");
            else if (c == ';') out.append("\\s");
            else out.append(c);
        }
        return out.toString();
    }

    private static String unescape(String s) {
        StringBuilder out = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c != '\\' || i + 1 >= s.length()) {
                out.append(c);
                continue;
            }
            char n = s.charAt(++i);
            if (n == '\\') out.append('\\');
            else if (n == 'n') out.append('\n');
            else if (n == 'r') out.append('\r');
            else if (n == 'e') out.append('=');
            else if (n == 's') out.append(';');
            else out.append(n);
        }
        return out.toString();
    }

    private void load() {
        if (mFile == null) return;
        final boolean readXml = mFile.exists();
        final File source = readXml ? mFile : mLegacyFile;
        if (source == null || !source.exists()) return;
        java.io.FileInputStream in = null;
        try {
            in = new java.io.FileInputStream(source);
            final long fileSize = source.length();
            if (fileSize <= 0 || fileSize > 16L * 1024L * 1024L) return;
            final int size = (int) fileSize;
            byte[] data = new byte[size];
            int read = 0;
            while (read < size) {
                int n = in.read(data, read, size - read);
                if (n <= 0) break;
                read += n;
            }
            final String text = new String(data, 0, read);
            if (readXml) parseXml(text);
            else parseLegacy(text);
        } catch (Throwable t) {
            // A corrupt or unreadable file must not stop the app: it starts with defaults,
            // exactly as a first launch would. Saying so is worth a line, though — silently
            // losing stored state is how a persisted identifier turns back into a new one.
            android.util.Log.e("SharedPreferences",
                    "could not load " + mName + ": " + t);
        } finally {
            if (in != null) { try { in.close(); } catch (Throwable ignored) {} }
        }
    }

    private void parseLegacy(String text) {
        int start = 0;
        while (start <= text.length()) {
            int end = text.indexOf('\n', start);
            if (end < 0) end = text.length();
            if (end > start) {
                parseLegacyLine(text.substring(start, end));
            }
            if (end == text.length()) break;
            start = end + 1;
        }
    }

    private void parseLegacyLine(String line) {
        if (line.length() < 3 || line.charAt(1) != ':') return;
        final char type = line.charAt(0);
        final int eq = line.indexOf('=', 2);
        if (eq < 0) return;
        final String key = unescape(line.substring(2, eq));
        final String raw = line.substring(eq + 1);
        try {
            if (type == 's') {
                mMap.put(key, unescape(raw));
            } else if (type == 'i') {
                mMap.put(key, Integer.valueOf(Integer.parseInt(raw)));
            } else if (type == 'l') {
                mMap.put(key, Long.valueOf(Long.parseLong(raw)));
            } else if (type == 'f') {
                mMap.put(key, Float.valueOf(Float.parseFloat(raw)));
            } else if (type == 'b') {
                mMap.put(key, Boolean.valueOf("1".equals(raw)));
            } else if (type == 'S') {
                Set<String> set = new LinkedHashSet<String>();
                int start = 0;
                while (start <= raw.length()) {
                    int semi = raw.indexOf(';', start);
                    if (semi < 0) semi = raw.length();
                    if (semi > start) set.add(unescape(raw.substring(start, semi)));
                    if (semi == raw.length()) break;
                    start = semi + 1;
                }
                mMap.put(key, set);
            }
        } catch (Throwable t) {
            // One malformed line loses one key, not the whole file.
        }
    }

    private static String escapeXml(String text) {
        StringBuilder out = new StringBuilder(text.length() + 8);
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '&') out.append("&amp;");
            else if (c == '<') out.append("&lt;");
            else if (c == '>') out.append("&gt;");
            else if (c == '"') out.append("&quot;");
            else if (c == '\'') out.append("&apos;");
            else if (c == '\n') out.append("&#10;");
            else if (c == '\r') out.append("&#13;");
            else if (c == '\t') out.append("&#9;");
            else out.append(c);
        }
        return out.toString();
    }

    private static String decodeXml(String text) {
        StringBuilder out = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c != '&') { out.append(c); continue; }
            int end = text.indexOf(';', i + 1);
            if (end < 0) { out.append(c); continue; }
            String entity = text.substring(i + 1, end);
            if ("amp".equals(entity)) out.append('&');
            else if ("lt".equals(entity)) out.append('<');
            else if ("gt".equals(entity)) out.append('>');
            else if ("quot".equals(entity)) out.append('"');
            else if ("apos".equals(entity)) out.append('\'');
            else if (entity.startsWith("#")) {
                try {
                    int radix = entity.startsWith("#x") || entity.startsWith("#X") ? 16 : 10;
                    int start = radix == 16 ? 2 : 1;
                    int cp = Integer.parseInt(entity.substring(start), radix);
                    if (cp >= 0 && cp <= 0x10ffff) {
                        if (cp <= 0xffff) out.append((char) cp);
                        else {
                            int value = cp - 0x10000;
                            out.append((char) (0xd800 | (value >> 10)));
                            out.append((char) (0xdc00 | (value & 0x3ff)));
                        }
                    }
                } catch (Throwable ignored) {}
            } else out.append('&').append(entity).append(';');
            i = end;
        }
        return out.toString();
    }

    private static int skipWhitespace(String text, int cursor) {
        while (cursor < text.length() && Character.isWhitespace(text.charAt(cursor))) cursor++;
        return cursor;
    }

    private static int findTagEnd(String text, int cursor) {
        char quote = 0;
        for (int i = cursor; i < text.length(); i++) {
            char c = text.charAt(i);
            if (quote != 0) { if (c == quote) quote = 0; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '>') return i;
        }
        return -1;
    }

    private static String tagName(String header) {
        int cursor = header.startsWith("<") ? 1 : 0;
        if (cursor < header.length() && header.charAt(cursor) == '/') cursor++;
        int start = cursor;
        while (cursor < header.length()) {
            char c = header.charAt(cursor);
            if (Character.isWhitespace(c) || c == '/' || c == '>') break;
            cursor++;
        }
        return header.substring(start, cursor);
    }

    private static String attribute(String header, String wanted) {
        int cursor = header.startsWith("<") ? 1 : 0;
        while (cursor < header.length() && !Character.isWhitespace(header.charAt(cursor)) &&
                header.charAt(cursor) != '/' && header.charAt(cursor) != '>') cursor++;
        while (cursor < header.length()) {
            cursor = skipWhitespace(header, cursor);
            if (cursor >= header.length() || header.charAt(cursor) == '/' ||
                    header.charAt(cursor) == '>') return null;
            int keyStart = cursor;
            while (cursor < header.length() && header.charAt(cursor) != '=' &&
                    !Character.isWhitespace(header.charAt(cursor)) &&
                    header.charAt(cursor) != '/' && header.charAt(cursor) != '>') cursor++;
            String key = header.substring(keyStart, cursor);
            cursor = skipWhitespace(header, cursor);
            if (cursor >= header.length() || header.charAt(cursor) != '=') continue;
            cursor = skipWhitespace(header, cursor + 1);
            if (cursor >= header.length()) return null;
            char quote = header.charAt(cursor++);
            if (quote != '\'' && quote != '"') return null;
            int valueStart = cursor;
            while (cursor < header.length() && header.charAt(cursor) != quote) cursor++;
            if (cursor >= header.length()) return null;
            String value = header.substring(valueStart, cursor++);
            if (wanted.equals(key)) return decodeXml(value);
        }
        return null;
    }

    private void parseXml(String text) {
        int mapStart = text.indexOf("<map");
        if (mapStart < 0) return;
        int rootEnd = findTagEnd(text, mapStart + 4);
        if (rootEnd < 0) return;
        if (!"map".equals(tagName(text.substring(mapStart, rootEnd + 1)))) return;
        int cursor = rootEnd + 1;
        while (cursor < text.length()) {
            cursor = skipWhitespace(text, cursor);
            if (cursor >= text.length() || text.startsWith("</map", cursor)) return;
            if (text.startsWith("<!--", cursor)) {
                int commentEnd = text.indexOf("-->", cursor + 4);
                if (commentEnd < 0) return;
                cursor = commentEnd + 3;
                continue;
            }
            if (text.charAt(cursor) != '<') { cursor++; continue; }
            int openEnd = findTagEnd(text, cursor + 1);
            if (openEnd < 0) return;
            String header = text.substring(cursor, openEnd + 1);
            String tag = tagName(header);
            String key = attribute(header, "name");
            boolean selfClosing = openEnd > cursor && text.charAt(openEnd - 1) == '/';
            int contentStart = openEnd + 1;
            if ("string".equals(tag)) {
                int close = selfClosing ? -1 : text.indexOf("</string>", contentStart);
                String value = selfClosing ? "" : close >= 0
                        ? decodeXml(text.substring(contentStart, close)) : null;
                if (key != null && value != null) mMap.put(key, value);
                cursor = selfClosing ? contentStart : close >= 0 ? close + 9 : text.length();
            } else if ("set".equals(tag)) {
                int close = selfClosing ? -1 : text.indexOf("</set>", contentStart);
                if (key != null && (selfClosing || close >= 0)) {
                    Set<String> values = new LinkedHashSet<String>();
                    if (!selfClosing) parseXmlSet(text.substring(contentStart, close), values);
                    mMap.put(key, values);
                }
                cursor = selfClosing ? contentStart : close >= 0 ? close + 6 : text.length();
            } else if ("int".equals(tag) || "long".equals(tag) || "float".equals(tag) ||
                       "boolean".equals(tag)) {
                String raw = attribute(header, "value");
                try {
                    if (key != null && raw != null) {
                        if ("int".equals(tag)) mMap.put(key, Integer.valueOf(Integer.parseInt(raw)));
                        else if ("long".equals(tag)) mMap.put(key, Long.valueOf(Long.parseLong(raw)));
                        else if ("float".equals(tag)) mMap.put(key, Float.valueOf(Float.parseFloat(raw)));
                        else mMap.put(key, Boolean.valueOf(Boolean.parseBoolean(raw)));
                    }
                } catch (Throwable ignored) {}
                if (!selfClosing) {
                    String closeTag = "</" + tag + ">";
                    int close = text.indexOf(closeTag, contentStart);
                    cursor = close >= 0 ? close + closeTag.length() : text.length();
                } else cursor = contentStart;
            } else cursor = contentStart;
        }
    }

    private static void parseXmlSet(String text, Set<String> values) {
        int cursor = 0;
        while (cursor < text.length()) {
            cursor = skipWhitespace(text, cursor);
            if (cursor >= text.length()) return;
            if (text.charAt(cursor) != '<') { cursor++; continue; }
            int openEnd = findTagEnd(text, cursor + 1);
            if (openEnd < 0) return;
            String header = text.substring(cursor, openEnd + 1);
            if (!"string".equals(tagName(header))) { cursor = openEnd + 1; continue; }
            if (openEnd > cursor && text.charAt(openEnd - 1) == '/') {
                values.add("");
                cursor = openEnd + 1;
                continue;
            }
            int close = text.indexOf("</string>", openEnd + 1);
            if (close < 0) return;
            values.add(decodeXml(text.substring(openEnd + 1, close)));
            cursor = close + 9;
        }
    }

    private String toXml(Map<String, Object> snapshot) {
        StringBuilder out = new StringBuilder(256);
        out.append("<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n");
        for (Map.Entry<String, Object> entry : snapshot.entrySet()) {
            String key = escapeXml(entry.getKey());
            Object value = entry.getValue();
            if (value instanceof String) {
                out.append("  <string name=\"").append(key).append("\">")
                   .append(escapeXml((String) value)).append("</string>\n");
            } else if (value instanceof Integer) {
                out.append("  <int name=\"").append(key).append("\" value=\"")
                   .append(value.toString()).append("\" />\n");
            } else if (value instanceof Long) {
                out.append("  <long name=\"").append(key).append("\" value=\"")
                   .append(value.toString()).append("\" />\n");
            } else if (value instanceof Float) {
                out.append("  <float name=\"").append(key).append("\" value=\"")
                   .append(value.toString()).append("\" />\n");
            } else if (value instanceof Boolean) {
                out.append("  <boolean name=\"").append(key).append("\" value=\"")
                   .append(((Boolean) value).booleanValue() ? "true" : "false")
                   .append("\" />\n");
            } else if (value instanceof Set) {
                out.append("  <set name=\"").append(key).append("\">\n");
                for (Object item : (Set<?>) value) {
                    out.append("    <string>")
                       .append(escapeXml(item == null ? "" : item.toString()))
                       .append("</string>\n");
                }
                out.append("  </set>\n");
            }
        }
        out.append("</map>\n");
        return out.toString();
    }

    private boolean save() {
        if (mFile == null) return true;
        synchronized (mDiskWriteLock) {
            Map<String, Object> snapshot = new HashMap<String, Object>();
            synchronized (mMap) {
                for (Map.Entry<String, Object> entry : mMap.entrySet()) {
                    Object value = entry.getValue();
                    if (value instanceof Set) value = new LinkedHashSet<String>((Set<String>) value);
                    snapshot.put(entry.getKey(), value);
                }
            }
            final File parent = mFile.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs() && !parent.isDirectory())
                return false;
            final File temp = new File(mFile.getPath() + ".tmp");
            java.io.FileOutputStream os = null;
            boolean saved = false;
            try {
                os = new java.io.FileOutputStream(temp);
                os.write(toXml(snapshot).getBytes("UTF-8"));
                os.close();
                os = null;
                saved = temp.renameTo(mFile);
                if (!saved) {
                    android.util.Log.e("SharedPreferences", "could not replace " + mFile.getPath());
                    return false;
                }
                if (mLegacyFile != null && mLegacyFile.exists() && !mLegacyFile.delete())
                    android.util.Log.w("SharedPreferences", "could not remove migrated " +
                            mLegacyFile.getPath());
                return true;
            } catch (Throwable t) {
                android.util.Log.e("SharedPreferences", "could not save " + mName + ": " + t);
                return false;
            } finally {
                if (os != null) { try { os.close(); } catch (Throwable ignored) {} }
                if (!saved && temp.exists()) temp.delete();
            }
        }
    }

    public final class EditorImpl implements Editor {
        private final Map<String, Object> mModified = new HashMap<String, Object>();
        private boolean mClear = false;

        public Editor putString(String key, String value) {
            synchronized (mModified) {
                // A null value means remove, per the SharedPreferences contract.
                mModified.put(key, value == null ? this : value);
            }
            return this;
        }
        public Editor putStringSet(String key, Set<String> values) {
            synchronized (mModified) {
                if (values == null) {
                    mModified.put(key, this);
                } else {
                    // A copy taken now: the caller may mutate or reuse its set after this
                    // returns, and storing the live reference would change what was saved.
                    mModified.put(key, new LinkedHashSet<String>(values));
                }
            }
            return this;
        }
        public Editor putInt(String key, int value) {
            synchronized (mModified) { mModified.put(key, Integer.valueOf(value)); }
            return this;
        }
        public Editor putLong(String key, long value) {
            synchronized (mModified) { mModified.put(key, Long.valueOf(value)); }
            return this;
        }
        public Editor putFloat(String key, float value) {
            synchronized (mModified) { mModified.put(key, Float.valueOf(value)); }
            return this;
        }
        public Editor putBoolean(String key, boolean value) {
            synchronized (mModified) { mModified.put(key, Boolean.valueOf(value)); }
            return this;
        }
        public Editor remove(String key) {
            // `this` marks a removal: no legitimate value can be the editor itself.
            synchronized (mModified) { mModified.put(key, this); }
            return this;
        }
        public Editor clear() {
            // Deferred to apply(), and it applies BEFORE the puts in the same editor — that
            // is the documented order, and clear() taking effect immediately would discard
            // values put before it.
            mClear = true;
            return this;
        }
        public boolean commit() { return applyInternal(); }
        public void apply() { applyInternal(); }

        private boolean applyInternal() {
            final List<String> changed = new ArrayList<String>();
            synchronized (mMap) {
                synchronized (mModified) {
                    if (mClear) {
                        changed.addAll(mMap.keySet());
                        mMap.clear();
                    }
                    for (Map.Entry<String, Object> e : mModified.entrySet()) {
                        final String key = e.getKey();
                        final Object value = e.getValue();
                        if (value == this) {
                            if (mMap.remove(key) != null) changed.add(key);
                        } else {
                            final Object old = mMap.put(key, value);
                            if (old == null || !old.equals(value)) changed.add(key);
                        }
                    }
                    mModified.clear();
                    mClear = false;
                }
            }
            boolean saved = save();
            notifyChanged(changed);
            return saved;
        }
    }
}
