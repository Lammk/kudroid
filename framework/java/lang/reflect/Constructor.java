package java.lang.reflect;

public final class Constructor<T> extends AccessibleObject {

    private long artMethod;
    private Class<T> declaringClass;

    private Constructor() {
    }

    public String getName() {
        return declaringClass.getName();
    }

    public Class<T> getDeclaringClass() {
        return declaringClass;
    }

    public native int getModifiers();

    public native Class<?>[] getParameterTypes();

    public native T newInstance(Object... args)
            throws InstantiationException, IllegalAccessException, InvocationTargetException;

    public int getParameterCount() {
        return getParameterTypes().length;
    }

    // isAccessible()/setAccessible() come from AccessibleObject, which is also the type
    // apps reference when they unlock several members at once.

    public Class<?>[] getExceptionTypes() {
        return new Class<?>[0];
    }

    public boolean equals(Object other) {
        return (other instanceof Constructor) && ((Constructor<?>) other).artMethod == artMethod;
    }

    public int hashCode() {
        return (int) (artMethod ^ (artMethod >>> 32));
    }

    /**
     * AOSP spelling: "public java.lang.StringBuilder(java.lang.String)".
     * Same reason as Method.toString: native bridges parse this string.
     */
    public String toString() {
        StringBuilder sb = new StringBuilder();
        int mod = getModifiers() & Modifier.constructorModifiers();
        if (mod != 0) {
            sb.append(Modifier.toString(mod)).append(' ');
        }
        sb.append(declaringClass.getTypeName()).append('(');
        Class<?>[] params = getParameterTypes();
        for (int i = 0; i < params.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(params[i].getTypeName());
        }
        sb.append(')');
        return sb.toString();
    }
}
