package java.lang.reflect;

/**
 * Handle to KuART's DexMethod. Field `artMethod` is written by native, not by Java
 * corrected.
 */
public final class Method extends AccessibleObject {

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

    /**
     * True for an interface method that carries a body.
     *
     * The three conditions are exactly the platform's: declared in an interface, not
     * static, and not abstract. A caller uses this to decide whether the method can be
     * invoked at all before reaching for MethodHandles.Lookup.unreflectSpecial — Unity's
     * JNIBridge tests it on every method its proxy handler receives, and answering wrongly
     * either skips a callable default or attempts to call an abstract declaration.
     */
    public native boolean isDefault();

    /**
     * True for a bridge method — a synthetic forwarder javac emits for a covariant
     * override or a generic erasure mismatch.
     *
     * Reported from the access flags rather than hardcoded false: reflection over a
     * generic class returns both the real method and its bridge, and code that dispatches
     * on getDeclaredMethods() has to be able to skip the bridge or it invokes the same
     * logic twice.
     */
    public native boolean isBridge();

    // isAccessible()/setAccessible() come from AccessibleObject, which is also the type
    // apps reference when they unlock several members at once.

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
