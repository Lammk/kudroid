package android.content;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * android.content.SharedPreferences, persisted to disk.
 *
 * In-memory is not sufficient here, and the reason is concrete: an app that generates an
 * identifier once and stores it — Minecraft's getLegacyDeviceID does exactly this — gets a
 * NEW one on every launch, so the installation looks like a different device each time.
 *
 * The on-disk format is a small text format rather than Android's XML: nothing outside
 * KuDroid reads these files, and a hand-rolled XML parser is a larger surface than the
 * problem needs. Each line is
 *
 *     <type>:<encoded key>=<encoded value>
 *
 * with newline, backslash and '=' escaped, so a key or value containing any of them cannot
 * forge a line boundary. Types are s/i/l/f/b/S (string, int, long, float, boolean, set),
 * and a set's members are separated by an unescaped ';'.
 */
public class SharedPreferencesImpl implements SharedPreferences {
    private final Map<String, Object> mMap = new HashMap<String, Object>();
    private final String mName;
    private final File mFile;
    private final List<OnSharedPreferenceChangeListener> mListeners =
            new ArrayList<OnSharedPreferenceChangeListener>();

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
        this.mFile = directory != null ? new File(directory, this.mName + ".prefs") : null;
        if (this.mFile != null) {
            load();
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

    // ── persistence ─────────────────────────────────────────────────────────

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
        if (mFile == null || !mFile.exists()) return;
        java.io.FileInputStream in = null;
        try {
            in = new java.io.FileInputStream(mFile);
            final int size = (int) mFile.length();
            if (size <= 0) return;
            byte[] data = new byte[size];
            int read = 0;
            while (read < size) {
                int n = in.read(data, read, size - read);
                if (n <= 0) break;
                read += n;
            }
            parse(new String(data, 0, read));
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

    private void parse(String text) {
        int start = 0;
        while (start <= text.length()) {
            int end = text.indexOf('\n', start);
            if (end < 0) end = text.length();
            if (end > start) {
                parseLine(text.substring(start, end));
            }
            if (end == text.length()) break;
            start = end + 1;
        }
    }

    private void parseLine(String line) {
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

    private void save() {
        if (mFile == null) return;
        StringBuilder out = new StringBuilder(256);
        synchronized (mMap) {
            for (Map.Entry<String, Object> e : mMap.entrySet()) {
                final String key = escape(e.getKey());
                final Object v = e.getValue();
                if (v instanceof String) {
                    out.append("s:").append(key).append('=').append(escape((String) v));
                } else if (v instanceof Integer) {
                    out.append("i:").append(key).append('=').append(v.toString());
                } else if (v instanceof Long) {
                    out.append("l:").append(key).append('=').append(v.toString());
                } else if (v instanceof Float) {
                    out.append("f:").append(key).append('=').append(v.toString());
                } else if (v instanceof Boolean) {
                    out.append("b:").append(key).append('=')
                       .append(((Boolean) v).booleanValue() ? "1" : "0");
                } else if (v instanceof Set) {
                    out.append("S:").append(key).append('=');
                    boolean first = true;
                    for (Object m : (Set<?>) v) {
                        if (!first) out.append(';');
                        out.append(escape(m == null ? "" : m.toString()));
                        first = false;
                    }
                } else {
                    continue;
                }
                out.append('\n');
            }
        }

        // Write to a temporary file and rename over the target.
        //
        // A direct write truncates first, so a crash mid-write leaves an empty file and the
        // stored state is gone — worse than not persisting at all, because the app already
        // believes it saved. rename is atomic, so a reader sees either the old file or the
        // new one.
        final File parent = mFile.getParentFile();
        if (parent != null && !parent.exists()) parent.mkdirs();
        final File temp = new File(mFile.getPath() + ".tmp");
        java.io.FileOutputStream os = null;
        try {
            os = new java.io.FileOutputStream(temp);
            os.write(out.toString().getBytes());
            os.close();
            os = null;
            if (!temp.renameTo(mFile)) {
                // renameTo does not replace on every filesystem; removing the target first
                // is the fallback, and it is still better than a truncating write because
                // the complete new contents already exist in temp.
                mFile.delete();
                if (!temp.renameTo(mFile)) {
                    android.util.Log.e("SharedPreferences",
                            "could not replace " + mFile.getPath());
                }
            }
        } catch (Throwable t) {
            android.util.Log.e("SharedPreferences", "could not save " + mName + ": " + t);
        } finally {
            if (os != null) { try { os.close(); } catch (Throwable ignored) {} }
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
            save();
            notifyChanged(changed);
            return true;
        }
    }
}
