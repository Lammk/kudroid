package org.json;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

public class JSONArray {
    private final List<Object> values;

    public JSONArray() { values = new ArrayList<Object>(); }
    public JSONArray(Collection copyFrom) {
        this();
        if (copyFrom != null) {
            for (Object it : copyFrom) values.add(it);
        }
    }
    public int length() { return values.size(); }
    public JSONArray put(boolean value) { values.add(value); return this; }
    public JSONArray put(double value) throws JSONException { values.add(value); return this; }
    public JSONArray put(int value) { values.add(value); return this; }
    public JSONArray put(long value) { values.add(value); return this; }
    public JSONArray put(Object value) { values.add(value); return this; }
    public JSONArray put(int index, Object value) throws JSONException {
        while (values.size() <= index) values.add(null);
        values.set(index, value);
        return this;
    }
    public boolean isNull(int index) {
        Object value = opt(index);
        return value == null || value == JSONObject.NULL;
    }
    public Object get(int index) throws JSONException {
        if (index < 0 || index >= values.size()) throw new JSONException("Index " + index + " out of range [0.." + values.size() + ")");
        return values.get(index);
    }
    public Object opt(int index) {
        if (index < 0 || index >= values.size()) return null;
        return values.get(index);
    }
    public Object remove(int index) {
        if (index < 0 || index >= values.size()) return null;
        return values.remove(index);
    }
    public boolean getBoolean(int index) throws JSONException { return (Boolean) get(index); }
    public double getDouble(int index) throws JSONException { return ((Number) get(index)).doubleValue(); }
    public int getInt(int index) throws JSONException { return ((Number) get(index)).intValue(); }
    public long getLong(int index) throws JSONException { return ((Number) get(index)).longValue(); }
    public String getString(int index) throws JSONException { return get(index).toString(); }
    public JSONObject getJSONObject(int index) throws JSONException { return (JSONObject) get(index); }
    public JSONArray getJSONArray(int index) throws JSONException { return (JSONArray) get(index); }
    public String toString() {
        StringBuilder sb = new StringBuilder("[");
        boolean first = true;
        for (Object val : values) {
            if (!first) sb.append(",");
            if (val instanceof String) sb.append("\"").append(val).append("\"");
            else sb.append(val);
            first = false;
        }
        sb.append("]");
        return sb.toString();
    }
}
