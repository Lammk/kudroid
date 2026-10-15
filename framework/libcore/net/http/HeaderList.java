package libcore.net.http;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** HTTP header fields in arrival order; names compare case-insensitively. */
public final class HeaderList {
    private final ArrayList<String> names = new ArrayList<String>();
    private final ArrayList<String> values = new ArrayList<String>();

    private static boolean sameName(String a, String b) {
        return a == null ? b == null : (b != null && a.equalsIgnoreCase(b));
    }

    public int size() { return names.size(); }
    public String name(int i) { return names.get(i); }
    public String value(int i) { return values.get(i); }

    public void add(String name, String value) {
        names.add(name);
        values.add(value);
    }

    /** Replaces every field called name with one. */
    public void set(String name, String value) {
        remove(name);
        add(name, value);
    }

    public void remove(String name) {
        for (int i = names.size() - 1; i >= 0; i--) {
            if (sameName(names.get(i), name)) {
                names.remove(i);
                values.remove(i);
            }
        }
    }

    /** The last value of name, or null. */
    public String get(String name) {
        for (int i = names.size() - 1; i >= 0; i--) {
            if (sameName(names.get(i), name)) return values.get(i);
        }
        return null;
    }

    public boolean has(String name) { return get(name) != null; }

    public List<String> getAll(String name) {
        ArrayList<String> out = new ArrayList<String>();
        for (int i = 0; i < names.size(); i++) {
            if (sameName(names.get(i), name)) out.add(values.get(i));
        }
        return out;
    }

    /** Name to values, keeping first-seen order; an unmodifiable view's worth of copies. */
    public Map<String, List<String>> toMap() {
        LinkedHashMap<String, List<String>> map = new LinkedHashMap<String, List<String>>();
        for (int i = 0; i < names.size(); i++) {
            String n = names.get(i);
            String key = null;
            for (String existing : map.keySet()) {
                if (sameName(existing, n)) {
                    key = existing;
                    break;
                }
            }
            if (key == null) {
                key = n;
                map.put(key, new ArrayList<String>());
            }
            map.get(key).add(values.get(i));
        }
        for (String k : map.keySet()) {
            map.put(k, Collections.unmodifiableList(map.get(k)));
        }
        return Collections.unmodifiableMap(map);
    }

    public HeaderList copy() {
        HeaderList c = new HeaderList();
        c.names.addAll(names);
        c.values.addAll(values);
        return c;
    }
}
