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

    /**
     * AOSP spelling: "public abstract void java.lang.Runnable.run()".
     *
     * Not cosmetic. Unity's native JNIBridge calls this on every proxied method
     * and parses the result; the old short form ("java.lang.Runnable.run()")
     * failed its parse and the bridge answered with
     * NoSuchMethodError("java.lang.Runnable.run()"), which escaped
     * ActivityThread.main and ended the session.
     */
    public String toString() {
        StringBuilder sb = new StringBuilder();
        int mod = getModifiers() & Modifier.methodModifiers();
        if (mod != 0) {
            sb.append(Modifier.toString(mod)).append(' ');
        }
        sb.append(getReturnType().getTypeName()).append(' ');
        sb.append(declaringClass.getTypeName()).append('.');
        sb.append(name).append('(');
        Class<?>[] params = getParameterTypes();
        for (int i = 0; i < params.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(params[i].getTypeName());
        }
        sb.append(')');
        return sb.toString();
    }
}
