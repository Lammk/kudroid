package android.os;

import android.util.ArrayMap;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.Set;

/**
 * A mapping from String keys to values of various types following AOSP contract.
 */
public class BaseBundle {
    protected ArrayMap<String, Object> mMap;
    private ClassLoader mClassLoader;

    public BaseBundle() {
        this((ClassLoader) null, 0);
    }

    public BaseBundle(ClassLoader loader) {
        this(loader, 0);
    }

    public BaseBundle(int capacity) {
        this((ClassLoader) null, capacity);
    }

    public BaseBundle(ClassLoader loader, int capacity) {
        mMap = capacity > 0 ? new ArrayMap<String, Object>(capacity) : new ArrayMap<String, Object>();
        mClassLoader = loader == null ? getClass().getClassLoader() : loader;
    }

    public BaseBundle(BaseBundle b) {
        copyInternal(b, false);
    }

    void copyInternal(BaseBundle from, boolean deep) {
        if (from == null) {
            mMap = new ArrayMap<String, Object>();
            return;
        }
        synchronized (from) {
            if (from.mMap != null) {
                mMap = new ArrayMap<String, Object>(from.mMap);
            } else {
                mMap = new ArrayMap<String, Object>();
            }
            mClassLoader = from.mClassLoader;
        }
    }

    public void setClassLoader(ClassLoader loader) {
        mClassLoader = loader;
    }

    public ClassLoader getClassLoader() {
        return mClassLoader;
    }

    public int size() {
        return mMap.size();
    }

    public boolean isEmpty() {
        return mMap.isEmpty();
    }

    public void clear() {
        mMap.clear();
    }

    public boolean containsKey(String key) {
        return mMap.containsKey(key);
    }

    public Object get(String key) {
        return mMap.get(key);
    }

    public void remove(String key) {
        mMap.remove(key);
    }

    public void putAll(PersistableBundle bundle) {
        if (bundle != null && bundle.mMap != null) {
            mMap.putAll(bundle.mMap);
        }
    }

    public Set<String> keySet() {
        return mMap.keySet();
    }

    public void putBoolean(String key, boolean value) {
        mMap.put(key, Boolean.valueOf(value));
    }

    public void putByte(String key, byte value) {
        mMap.put(key, Byte.valueOf(value));
    }

    public void putChar(String key, char value) {
        mMap.put(key, Character.valueOf(value));
    }

    public void putShort(String key, short value) {
        mMap.put(key, Short.valueOf(value));
    }

    public void putInt(String key, int value) {
        mMap.put(key, Integer.valueOf(value));
    }

    public void putLong(String key, long value) {
        mMap.put(key, Long.valueOf(value));
    }

    public void putFloat(String key, float value) {
        mMap.put(key, Float.valueOf(value));
    }

    public void putDouble(String key, double value) {
        mMap.put(key, Double.valueOf(value));
    }

    public void putString(String key, String value) {
        mMap.put(key, value);
    }

    public void putCharSequence(String key, CharSequence value) {
        mMap.put(key, value);
    }

    public void putIntegerArrayList(String key, ArrayList<Integer> value) {
        mMap.put(key, value);
    }

    public void putStringArrayList(String key, ArrayList<String> value) {
        mMap.put(key, value);
    }

    public void putCharSequenceArrayList(String key, ArrayList<CharSequence> value) {
        mMap.put(key, value);
    }

    public void putSerializable(String key, Serializable value) {
        mMap.put(key, value);
    }

    public void putBooleanArray(String key, boolean[] value) {
        mMap.put(key, value);
    }

    public void putByteArray(String key, byte[] value) {
        mMap.put(key, value);
    }

    public void putShortArray(String key, short[] value) {
        mMap.put(key, value);
    }

    public void putCharArray(String key, char[] value) {
        mMap.put(key, value);
    }

    public void putIntArray(String key, int[] value) {
        mMap.put(key, value);
    }

    public void putLongArray(String key, long[] value) {
        mMap.put(key, value);
    }

    public void putFloatArray(String key, float[] value) {
        mMap.put(key, value);
    }

    public void putDoubleArray(String key, double[] value) {
        mMap.put(key, value);
    }

    public void putStringArray(String key, String[] value) {
        mMap.put(key, value);
    }

    public void putCharSequenceArray(String key, CharSequence[] value) {
        mMap.put(key, value);
    }

    public boolean getBoolean(String key) {
        return getBoolean(key, false);
    }

    public boolean getBoolean(String key, boolean defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Boolean) return ((Boolean) o).booleanValue();
        return defaultValue;
    }

    public byte getByte(String key) {
        return getByte(key, (byte) 0);
    }

    public byte getByte(String key, byte defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Byte) return ((Byte) o).byteValue();
        return defaultValue;
    }

    public char getChar(String key) {
        return getChar(key, (char) 0);
    }

    public char getChar(String key, char defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Character) return ((Character) o).charValue();
        return defaultValue;
    }

    public short getShort(String key) {
        return getShort(key, (short) 0);
    }

    public short getShort(String key, short defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Short) return ((Short) o).shortValue();
        return defaultValue;
    }

    public int getInt(String key) {
        return getInt(key, 0);
    }

    public int getInt(String key, int defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Integer) return ((Integer) o).intValue();
        return defaultValue;
    }

    public long getLong(String key) {
        return getLong(key, 0L);
    }

    public long getLong(String key, long defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Long) return ((Long) o).longValue();
        return defaultValue;
    }

    public float getFloat(String key) {
        return getFloat(key, 0.0f);
    }

    public float getFloat(String key, float defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Float) return ((Float) o).floatValue();
        return defaultValue;
    }

    public double getDouble(String key) {
        return getDouble(key, 0.0);
    }

    public double getDouble(String key, double defaultValue) {
        Object o = mMap.get(key);
        if (o instanceof Double) return ((Double) o).doubleValue();
        return defaultValue;
    }

    public String getString(String key) {
        Object o = mMap.get(key);
        return (o instanceof String) ? (String) o : null;
    }

    public String getString(String key, String defaultValue) {
        String s = getString(key);
        return (s != null) ? s : defaultValue;
    }

    public CharSequence getCharSequence(String key) {
        Object o = mMap.get(key);
        return (o instanceof CharSequence) ? (CharSequence) o : null;
    }

    public CharSequence getCharSequence(String key, CharSequence defaultValue) {
        CharSequence s = getCharSequence(key);
        return (s != null) ? s : defaultValue;
    }

    public Serializable getSerializable(String key) {
        Object o = mMap.get(key);
        return (o instanceof Serializable) ? (Serializable) o : null;
    }

    @SuppressWarnings("unchecked")
    public ArrayList<Integer> getIntegerArrayList(String key) {
        Object o = mMap.get(key);
        return (o instanceof ArrayList) ? (ArrayList<Integer>) o : null;
    }

    @SuppressWarnings("unchecked")
    public ArrayList<String> getStringArrayList(String key) {
        Object o = mMap.get(key);
        return (o instanceof ArrayList) ? (ArrayList<String>) o : null;
    }

    @SuppressWarnings("unchecked")
    public ArrayList<CharSequence> getCharSequenceArrayList(String key) {
        Object o = mMap.get(key);
        return (o instanceof ArrayList) ? (ArrayList<CharSequence>) o : null;
    }

    public boolean[] getBooleanArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof boolean[]) ? (boolean[]) o : null;
    }

    public byte[] getByteArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof byte[]) ? (byte[]) o : null;
    }

    public short[] getShortArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof short[]) ? (short[]) o : null;
    }

    public char[] getCharArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof char[]) ? (char[]) o : null;
    }

    public int[] getIntArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof int[]) ? (int[]) o : null;
    }

    public long[] getLongArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof long[]) ? (long[]) o : null;
    }

    public float[] getFloatArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof float[]) ? (float[]) o : null;
    }

    public double[] getDoubleArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof double[]) ? (double[]) o : null;
    }

    public String[] getStringArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof String[]) ? (String[]) o : null;
    }

    public CharSequence[] getCharSequenceArray(String key) {
        Object o = mMap.get(key);
        return (o instanceof CharSequence[]) ? (CharSequence[]) o : null;
    }
}
