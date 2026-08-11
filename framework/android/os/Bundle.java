package android.os;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/**
 * Minimal android.os.Bundle implementation.
 *
 * A mapping from String keys to various typed values. Used to pass data
 * between Activities and Intents. For KuDroid's minimal framework, we back it
 * with a HashMap.
 */
public final class Bundle {
    private Map<String, Object> mMap;

    public Bundle() {
        mMap = new HashMap<String, Object>();
    }

    public Bundle(Bundle b) {
        mMap = new HashMap<String, Object>();
        if (b != null && b.mMap != null) {
            mMap.putAll(b.mMap);
        }
    }

    public void putString(String key, String value) {
        mMap.put(key, value);
    }

    public String getString(String key) {
        Object v = mMap.get(key);
        return (v instanceof String) ? (String) v : null;
    }

    public String getString(String key, String defaultValue) {
        String v = getString(key);
        return (v != null) ? v : defaultValue;
    }

    public void putInt(String key, int value) {
        mMap.put(key, Integer.valueOf(value));
    }

    public int getInt(String key) {
        return getInt(key, 0);
    }

    public int getInt(String key, int defaultValue) {
        Object v = mMap.get(key);
        return (v instanceof Integer) ? ((Integer) v).intValue() : defaultValue;
    }

    public void putLong(String key, long value) {
        mMap.put(key, Long.valueOf(value));
    }

    public long getLong(String key) {
        return getLong(key, 0L);
    }

    public long getLong(String key, long defaultValue) {
        Object v = mMap.get(key);
        return (v instanceof Long) ? ((Long) v).longValue() : defaultValue;
    }

    public void putBoolean(String key, boolean value) {
        mMap.put(key, Boolean.valueOf(value));
    }

    public boolean getBoolean(String key) {
        return getBoolean(key, false);
    }

    public boolean getBoolean(String key, boolean defaultValue) {
        Object v = mMap.get(key);
        return (v instanceof Boolean) ? ((Boolean) v).booleanValue() : defaultValue;
    }

    public void putFloat(String key, float value) {
        mMap.put(key, Float.valueOf(value));
    }

    public float getFloat(String key) {
        return getFloat(key, 0.0f);
    }

    public float getFloat(String key, float defaultValue) {
        Object v = mMap.get(key);
        return (v instanceof Float) ? ((Float) v).floatValue() : defaultValue;
    }

    public void putDouble(String key, double value) {
        mMap.put(key, Double.valueOf(value));
    }

    public double getDouble(String key) {
        return getDouble(key, 0.0);
    }

    public double getDouble(String key, double defaultValue) {
        Object v = mMap.get(key);
        return (v instanceof Double) ? ((Double) v).doubleValue() : defaultValue;
    }

    public void putSerializable(String key, java.io.Serializable value) {
        mMap.put(key, value);
    }

    /**
     * Copy all mappings from the given bundle into this bundle.
     */
    public void putAll(Bundle bundle) {
        if (bundle != null && bundle.mMap != null) {
            mMap.putAll(bundle.mMap);
        }
    }

    public java.io.Serializable getSerializable(String key) {
        Object v = mMap.get(key);
        return (v instanceof java.io.Serializable) ? (java.io.Serializable) v : null;
    }

    public boolean containsKey(String key) {
        return mMap.containsKey(key);
    }

    public Set<String> keySet() {
        return mMap.keySet();
    }

    public boolean isEmpty() {
        return mMap.isEmpty();
    }

    public int size() {
        return mMap.size();
    }

    public void clear() {
        mMap.clear();
    }
}
