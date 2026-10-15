package java.lang.reflect;

/** Handle to KuART's DexField; `artField` written by native. */
public final class Field extends AccessibleObject {

    private long artField;
    private Class<?> declaringClass;
    private String name;

    private Field() {
    }

    public String getName() {
        return name;
    }

    public Class<?> getDeclaringClass() {
        return declaringClass;
    }

    public native int getModifiers();

    public native Class<?> getType();

    public native Object get(Object obj) throws IllegalAccessException;

    public native void set(Object obj, Object value) throws IllegalAccessException;

    public int getInt(Object obj) throws IllegalAccessException {
        return ((Integer) get(obj)).intValue();
    }

    public long getLong(Object obj) throws IllegalAccessException {
        return ((Long) get(obj)).longValue();
    }

    public float getFloat(Object obj) throws IllegalAccessException {
        return ((Float) get(obj)).floatValue();
    }

    public double getDouble(Object obj) throws IllegalAccessException {
        return ((Double) get(obj)).doubleValue();
    }

    public boolean getBoolean(Object obj) throws IllegalAccessException {
        return ((Boolean) get(obj)).booleanValue();
    }

    public char getChar(Object obj) throws IllegalAccessException {
        return ((Character) get(obj)).charValue();
    }

    public void setInt(Object obj, int value) throws IllegalAccessException {
        set(obj, Integer.valueOf(value));
    }

    public void setLong(Object obj, long value) throws IllegalAccessException {
        set(obj, Long.valueOf(value));
    }

    public void setFloat(Object obj, float value) throws IllegalAccessException {
        set(obj, Float.valueOf(value));
    }

    public void setDouble(Object obj, double value) throws IllegalAccessException {
        set(obj, Double.valueOf(value));
    }

    public void setBoolean(Object obj, boolean value) throws IllegalAccessException {
        set(obj, Boolean.valueOf(value));
    }

    public void setChar(Object obj, char value) throws IllegalAccessException {
        set(obj, Character.valueOf(value));
    }

    // isAccessible()/setAccessible() come from AccessibleObject, which is also the type
    // apps reference when they unlock several members at once.

    public boolean isSynthetic() {
        return false;
    }

    public boolean equals(Object other) {
        return (other instanceof Field) && ((Field) other).artField == artField;
    }

    public int hashCode() {
        return (int) (artField ^ (artField >>> 32));
    }

    /**
     * AOSP spelling: "private static final java.lang.String java.lang.Foo.bar".
     * Same reason as Method.toString: native bridges parse this string.
     */
    public String toString() {
        StringBuilder sb = new StringBuilder();
        int mod = getModifiers() & Modifier.fieldModifiers();
        if (mod != 0) {
            sb.append(Modifier.toString(mod)).append(' ');
        }
        sb.append(getType().getTypeName()).append(' ');
        sb.append(declaringClass.getTypeName()).append('.');
        sb.append(name);
        return sb.toString();
    }
}
