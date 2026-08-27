package java.lang.reflect;

public final class Constructor<T> {

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

    public boolean isAccessible() {
        return true;
    }

    public void setAccessible(boolean flag) {
    }

    public Class<?>[] getExceptionTypes() {
        return new Class<?>[0];
    }

    public boolean equals(Object other) {
        return (other instanceof Constructor) && ((Constructor<?>) other).artMethod == artMethod;
    }

    public int hashCode() {
        return (int) (artMethod ^ (artMethod >>> 32));
    }

    public String toString() {
        return declaringClass.getName() + ".<init>()";
    }
}
