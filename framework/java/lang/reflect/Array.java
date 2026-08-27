package java.lang.reflect;

public final class Array {

    private Array() {
    }

    public static native Object newInstance(Class<?> componentType, int length);

    public static native int getLength(Object array);

    public static native Object get(Object array, int index);

    public static native void set(Object array, int index, Object value);

    public static int getInt(Object array, int index) {
        return ((Integer) get(array, index)).intValue();
    }

    public static long getLong(Object array, int index) {
        return ((Long) get(array, index)).longValue();
    }

    public static float getFloat(Object array, int index) {
        return ((Float) get(array, index)).floatValue();
    }

    public static double getDouble(Object array, int index) {
        return ((Double) get(array, index)).doubleValue();
    }

    public static boolean getBoolean(Object array, int index) {
        return ((Boolean) get(array, index)).booleanValue();
    }

    public static char getChar(Object array, int index) {
        return ((Character) get(array, index)).charValue();
    }

    public static void setInt(Object array, int index, int value) {
        set(array, index, Integer.valueOf(value));
    }

    public static void setLong(Object array, int index, long value) {
        set(array, index, Long.valueOf(value));
    }

    public static void setFloat(Object array, int index, float value) {
        set(array, index, Float.valueOf(value));
    }

    public static void setDouble(Object array, int index, double value) {
        set(array, index, Double.valueOf(value));
    }

    public static void setBoolean(Object array, int index, boolean value) {
        set(array, index, Boolean.valueOf(value));
    }

    public static void setChar(Object array, int index, char value) {
        set(array, index, Character.valueOf(value));
    }
}
