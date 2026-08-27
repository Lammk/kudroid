package android.content;

import java.util.HashMap;
import java.util.Map;

/**
 * in-memory implementation of sharedpreferences.
 *
 * for kudroid minimal framework, values ​​are only kept in memory and not
 * is saved to disk. This is enough for applications that read options at times
 * boots and does not require them to persist after reboot.
 */
public class SharedPreferencesImpl implements SharedPreferences {
    private final Map<String, Object> mMap = new HashMap<String, Object>();

    public SharedPreferencesImpl() {
    }

    @Override
    public String getString(String key, String defValue) {
        Object v = mMap.get(key);
        return (v instanceof String) ? (String) v : defValue;
    }

    @Override
    public int getInt(String key, int defValue) {
        Object v = mMap.get(key);
        return (v instanceof Integer) ? ((Integer) v).intValue() : defValue;
    }

    @Override
    public long getLong(String key, long defValue) {
        Object v = mMap.get(key);
        return (v instanceof Long) ? ((Long) v).longValue() : defValue;
    }

    @Override
    public float getFloat(String key, float defValue) {
        Object v = mMap.get(key);
        return (v instanceof Float) ? ((Float) v).floatValue() : defValue;
    }

    @Override
    public boolean getBoolean(String key, boolean defValue) {
        Object v = mMap.get(key);
        return (v instanceof Boolean) ? ((Boolean) v).booleanValue() : defValue;
    }

    @Override
    public boolean contains(String key) {
        return mMap.containsKey(key);
    }

    @Override
    public Editor edit() {
        return new EditorImpl();
    }

    private final class EditorImpl implements Editor {
        private final Map<String, Object> mModified = new HashMap<String, Object>();

        @Override
        public Editor putString(String key, String value) {
            mModified.put(key, value);
            return this;
        }

        @Override
        public Editor putInt(String key, int value) {
            mModified.put(key, Integer.valueOf(value));
            return this;
        }

        @Override
        public Editor putLong(String key, long value) {
            mModified.put(key, Long.valueOf(value));
            return this;
        }

        @Override
        public Editor putFloat(String key, float value) {
            mModified.put(key, Float.valueOf(value));
            return this;
        }

        @Override
        public Editor putBoolean(String key, boolean value) {
            mModified.put(key, Boolean.valueOf(value));
            return this;
        }

        @Override
        public Editor remove(String key) {
            mModified.put(key, null);
            return this;
        }

        @Override
        public Editor clear() {
            mModified.clear();
            mMap.clear();
            return this;
        }

        @Override
        public boolean commit() {
            synchronized (mMap) {
                for (Map.Entry<String, Object> e : mModified.entrySet()) {
                    if (e.getValue() == null) {
                        mMap.remove(e.getKey());
                    } else {
                        mMap.put(e.getKey(), e.getValue());
                    }
                }
                mModified.clear();
            }
            return true;
        }

        @Override
        public void apply() {
            commit();
        }
    }
}
