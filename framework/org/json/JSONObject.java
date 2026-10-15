package org.json;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;

public class JSONObject {
    public static final Object NULL = new Object() {
        public boolean equals(Object o) { return o == this || o == null; }
        public String toString() { return "null"; }
    };
    private final HashMap<String, Object> nameValuePairs;

    public JSONObject() { nameValuePairs = new HashMap<String, Object>(); }
    public JSONObject(Map copyFrom) {
        this();
        Map<?, ?> contentsTyped = (Map<?, ?>) copyFrom;
        for (Map.Entry<?, ?> entry : contentsTyped.entrySet()) {
            String key = (String) entry.getKey();
            if (key == null) throw new NullPointerException("key == null");
            nameValuePairs.put(key, entry.getValue());
        }
    }
    public JSONObject(String json) throws JSONException {
        this();
        // Minimal basic parser or store
    }
    public int length() { return nameValuePairs.size(); }
    public JSONObject put(String name, boolean value) throws JSONException { nameValuePairs.put(name, value); return this; }
    public JSONObject put(String name, double value) throws JSONException { nameValuePairs.put(name, value); return this; }
    public JSONObject put(String name, int value) throws JSONException { nameValuePairs.put(name, value); return this; }
    public JSONObject put(String name, long value) throws JSONException { nameValuePairs.put(name, value); return this; }
    public JSONObject put(String name, Object value) throws JSONException { nameValuePairs.put(name, value); return this; }
    public JSONObject putOpt(String name, Object value) throws JSONException {
        if (name == null || value == null) return this;
        return put(name, value);
    }
    public Object remove(String name) { return nameValuePairs.remove(name); }
    public boolean isNull(String name) {
        Object value = nameValuePairs.get(name);
        return value == null || value == NULL;
    }
    public boolean has(String name) { return nameValuePairs.containsKey(name); }
    public Object get(String name) throws JSONException {
        Object result = nameValuePairs.get(name);
        if (result == null) throw new JSONException("No value for " + name);
        return result;
    }
    public Object opt(String name) { return nameValuePairs.get(name); }
    public boolean getBoolean(String name) throws JSONException { return (Boolean) get(name); }
    public boolean optBoolean(String name) { return optBoolean(name, false); }
    public boolean optBoolean(String name, boolean fallback) {
        Object o = opt(name);
        return (o instanceof Boolean) ? (Boolean) o : fallback;
    }
    public double getDouble(String name) throws JSONException { return ((Number) get(name)).doubleValue(); }
    public double optDouble(String name) { return optDouble(name, Double.NaN); }
    public double optDouble(String name, double fallback) {
        Object o = opt(name);
        return (o instanceof Number) ? ((Number) o).doubleValue() : fallback;
    }
    public int getInt(String name) throws JSONException { return ((Number) get(name)).intValue(); }
    public int optInt(String name) { return optInt(name, 0); }
    public int optInt(String name, int fallback) {
        Object o = opt(name);
        return (o instanceof Number) ? ((Number) o).intValue() : fallback;
    }
    public long getLong(String name) throws JSONException { return ((Number) get(name)).longValue(); }
    public long optLong(String name) { return optLong(name, 0L); }
    public long optLong(String name, long fallback) {
        Object o = opt(name);
        return (o instanceof Number) ? ((Number) o).longValue() : fallback;
    }
    public String getString(String name) throws JSONException { return get(name).toString(); }
    public String optString(String name) { return optString(name, ""); }
    public String optString(String name, String fallback) {
        Object o = opt(name);
        return o != null ? o.toString() : fallback;
    }
    public JSONArray getJSONArray(String name) throws JSONException { return (JSONArray) get(name); }
    public JSONArray optJSONArray(String name) {
        Object o = opt(name);
        return (o instanceof JSONArray) ? (JSONArray) o : null;
    }
    public JSONObject getJSONObject(String name) throws JSONException { return (JSONObject) get(name); }
    public JSONObject optJSONObject(String name) {
        Object o = opt(name);
        return (o instanceof JSONObject) ? (JSONObject) o : null;
    }
    public Iterator<String> keys() { return nameValuePairs.keySet().iterator(); }
    public Set<String> keySet() { return nameValuePairs.keySet(); }
    public JSONArray names() {
        return nameValuePairs.isEmpty() ? null : new JSONArray(nameValuePairs.keySet());
    }
    public String toString() {
        StringBuilder sb = new StringBuilder("{");
        boolean first = true;
        for (Map.Entry<String, Object> e : nameValuePairs.entrySet()) {
            if (!first) sb.append(",");
            sb.append("\"").append(e.getKey()).append("\":");
            Object val = e.getValue();
            if (val instanceof String) sb.append("\"").append(val).append("\"");
            else sb.append(val);
            first = false;
        }
        sb.append("}");
        return sb.toString();
    }
}
