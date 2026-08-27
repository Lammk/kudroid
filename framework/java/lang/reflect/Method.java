package java.lang.reflect;

/**
 * Handle to KuART's DexMethod. Field `artMethod` is written by native, not by Java
 * corrected.
 */
public final class Method {

    private long artMethod;
    private Class<?> declaringClass;
    private String name;

    private Method() {
    }

    public String getName() {
        return name;
    }

    public Class<?> getDeclaringClass() {
        return declaringClass;
    }

    public native int getModifiers();

    public native Class<?> getReturnType();

    public native Class<?>[] getParameterTypes();

    public native Object invoke(Object receiver, Object... args)
            throws IllegalAccessException, InvocationTargetException;

    public int getParameterCount() {
        return getParameterTypes().length;
    }

    public boolean isAccessible() {
        return true;
    }

    public void setAccessible(boolean flag) {
    }

    public boolean isVarArgs() {
        return false;
    }

    public boolean isSynthetic() {
        return false;
    }

    public Class<?>[] getExceptionTypes() {
        return new Class<?>[0];
    }

    public boolean equals(Object other) {
        return (other instanceof Method) && ((Method) other).artMethod == artMethod;
    }

    public int hashCode() {
        return (int) (artMethod ^ (artMethod >>> 32));
    }

    public String toString() {
        return declaringClass.getName() + "." + name + "()";
    }
}
