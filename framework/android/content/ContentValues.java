package android.content;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/**
 * triển khai android.content.contentvalues tối thiểu.
 *
 * bản đồ của tên cột với giá trị, được sử dụng cùng contentresolver. đối với khuôn khổ
 * tối thiểu của kudroid, được hỗ trợ bởi một hashmap.
 */
public final class ContentValues {
    private final Map<String, Object> mValues = new HashMap<String, Object>();

    public ContentValues() {
    }

    public ContentValues(int size) {
    }

    public ContentValues(ContentValues from) {
        if (from != null) {
            mValues.putAll(from.mValues);
        }
    }

    public void put(String key, String value) {
        mValues.put(key, value);
    }

    public void put(String key, Integer value) {
        mValues.put(key, value);
    }

    public void put(String key, Long value) {
        mValues.put(key, value);
    }

    public void put(String key, Float value) {
        mValues.put(key, value);
    }

    public void put(String key, Double value) {
        mValues.put(key, value);
    }

    public void put(String key, Boolean value) {
        mValues.put(key, value);
    }

    public void putNull(String key) {
        mValues.put(key, null);
    }

    public String getAsString(String key) {
        Object v = mValues.get(key);
        return v == null ? null : v.toString();
    }

    public Integer getAsInteger(String key) {
        Object v = mValues.get(key);
        return (v instanceof Integer) ? (Integer) v : null;
    }

    public Long getAsLong(String key) {
        Object v = mValues.get(key);
        return (v instanceof Long) ? (Long) v : null;
    }

    public Float getAsFloat(String key) {
        Object v = mValues.get(key);
        return (v instanceof Float) ? (Float) v : null;
    }

    public Double getAsDouble(String key) {
        Object v = mValues.get(key);
        return (v instanceof Double) ? (Double) v : null;
    }

    public Boolean getAsBoolean(String key) {
        Object v = mValues.get(key);
        return (v instanceof Boolean) ? (Boolean) v : null;
    }

    public boolean containsKey(String key) {
        return mValues.containsKey(key);
    }

    public Set<String> keySet() {
        return mValues.keySet();
    }

    public int size() {
        return mValues.size();
    }

    public void clear() {
        mValues.clear();
    }
}
