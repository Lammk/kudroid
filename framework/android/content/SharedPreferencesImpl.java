package android.content;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

public class SharedPreferencesImpl implements SharedPreferences {
    private final Map<String, Object> mMap = new HashMap<String, Object>();
    private final String mName;

    public SharedPreferencesImpl() { this.mName = "default"; }
    public SharedPreferencesImpl(String name) { this.mName = name; }

    public Map<String, ?> getAll() { return new HashMap<String, Object>(mMap); }
    public String getString(String key, String defValue) { Object v = mMap.get(key); return (v instanceof String) ? (String) v : defValue; }
    public Set<String> getStringSet(String key, Set<String> defValues) { return defValues; }
    public int getInt(String key, int defValue) { Object v = mMap.get(key); return (v instanceof Number) ? ((Number) v).intValue() : defValue; }
    public long getLong(String key, long defValue) { Object v = mMap.get(key); return (v instanceof Number) ? ((Number) v).longValue() : defValue; }
    public float getFloat(String key, float defValue) { Object v = mMap.get(key); return (v instanceof Number) ? ((Number) v).floatValue() : defValue; }
    public boolean getBoolean(String key, boolean defValue) { Object v = mMap.get(key); return (v instanceof Boolean) ? (Boolean) v : defValue; }
    public boolean contains(String key) { return mMap.containsKey(key); }
    public Editor edit() { return new EditorImpl(); }
    public void registerOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {}
    public void unregisterOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {}

    public final class EditorImpl implements Editor {
        private final Map<String, Object> mModified = new HashMap<String, Object>();
        public Editor putString(String key, String value) { mModified.put(key, value); return this; }
        public Editor putStringSet(String key, Set<String> values) { return this; }
        public Editor putInt(String key, int value) { mModified.put(key, value); return this; }
        public Editor putLong(String key, long value) { mModified.put(key, value); return this; }
        public Editor putFloat(String key, float value) { mModified.put(key, value); return this; }
        public Editor putBoolean(String key, boolean value) { mModified.put(key, value); return this; }
        public Editor remove(String key) { mModified.put(key, this); return this; }
        public Editor clear() { return this; }
        public boolean commit() { apply(); return true; }
        public void apply() {
            synchronized (mMap) {
                for (Map.Entry<String, Object> e : mModified.entrySet()) {
                    if (e.getValue() == this) mMap.remove(e.getKey());
                    else mMap.put(e.getKey(), e.getValue());
                }
            }
        }
    }
}
