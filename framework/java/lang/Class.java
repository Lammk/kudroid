package java.lang;

/**
 * Class object. KuART allocates a separate DexClassObject for each class
 * This class does not have field instances; All information is obtained through native.
 */
public final class Class<T> {

    private Class() {
    }

    public boolean desiredAssertionStatus() {
        return false;
    }

    public native String getName();

    public native Class<? super T> getSuperclass();

    public native Class<?>[] getInterfaces();

    public native boolean isInterface();

    public native boolean isArray();

    public native boolean isPrimitive();

    public native boolean isEnum();

    public native int getModifiers();

    public native Class<?> getComponentType();

    public native boolean isInstance(Object obj);

    public native boolean isAssignableFrom(Class<?> other);

    public native T newInstance() throws InstantiationException, IllegalAccessException;

    public native ClassLoader getClassLoader();

    public native java.lang.reflect.Method[] getDeclaredMethods();

    public native java.lang.reflect.Field[] getDeclaredFields();

    public native java.lang.reflect.Constructor<T>[] getDeclaredConstructors();

    public static native Class<?> forName(String className) throws ClassNotFoundException;

    public static Class<?> forName(String className, boolean initialize, ClassLoader loader)
            throws ClassNotFoundException {
        return forName(className);
    }

    /**
     * The Class object for a primitive type, named the way Java source names it
     * ("int", "void", ...).
     *
     * Not an API apps call directly. It exists because javac compiles every primitive
     * class literal into a read of the matching box class's TYPE field — {@code int.class}
     * is {@code getstatic java/lang/Integer.TYPE}, {@code void.class} is
     * {@code getstatic java/lang/Void.TYPE} — so this one method backs all nine primitive
     * literals appearing in guest bytecode.
     *
     * Those TYPE fields used to be initialised to null, and the damage was much wider than
     * a null literal. getMethod, getDeclaredConstructor and getDeclaredMethod all compare
     * parameter types by reference identity: one side came from the DEX signature and was a
     * real Class for descriptor "I", the other was the app's {@code int.class} and was
     * null. They never matched, so EVERY reflective lookup whose signature mentioned a
     * primitive failed with NoSuchMethodException — for any app, with nothing in the
     * message pointing at the literal that was actually empty.
     */
    static native Class<?> getPrimitiveClass(String name);

    public String getSimpleName() {
        String name = getName();
        int dollar = name.lastIndexOf('$');
        if (dollar >= 0) {
            return name.substring(dollar + 1);
        }
        int dot = name.lastIndexOf('.');
        return dot >= 0 ? name.substring(dot + 1) : name;
    }

    public String getCanonicalName() {
        return getName().replace('$', '.');
    }

    public String getPackageName() {
        String name = getName();
        int dot = name.lastIndexOf('.');
        return dot >= 0 ? name.substring(0, dot) : "";
    }

    /**
     * The package this class belongs to.
     *
     * Was auto-stubbed to null because java.lang.Package did not exist, and the
     * common idiom getClass().getPackage().getImplementationVersion() then threw a
     * NullPointerException. Returns null only for a class in the default package,
     * which is what the JVM does and what callers check for.
     */
    public Package getPackage() {
        String pkg = getPackageName();
        if (pkg == null || pkg.isEmpty()) return null;
        return new Package(pkg);
    }

    public boolean isAnnotation() {
        return false;
    }

    public boolean isSynthetic() {
        return false;
    }

    public T cast(Object obj) {
        if (obj != null && !isInstance(obj)) {
            throw new ClassCastException(obj.getClass().getName() + " not " + getName());
        }
        return (T) obj;
    }

    public java.lang.reflect.Method getDeclaredMethod(String name, Class<?>... parameterTypes)
            throws NoSuchMethodException {
        java.lang.reflect.Method[] all = getDeclaredMethods();
        for (int i = 0; i < all.length; i++) {
            if (all[i].getName().equals(name) && matches(all[i].getParameterTypes(), parameterTypes)) {
                return all[i];
            }
        }
        throw new NoSuchMethodException(getName() + "." + name);
    }

    /**
     * A public method by name and parameter types, from this class OR any interface
     * it implements.
     *
     * The interface half is not optional. This used to walk only getSuperclass(),
     * and a Proxy created for Runnable declares nothing itself while Proxy and
     * Object declare no run() either — so run() was invisible and Unity's JNIBridge
     * concluded the method did not exist, threw NoSuchMethodError("Runnable.run()")
     * through JNI ThrowNew, cleared it, and rethrew an empty one from
     * bitter.jnibridge.a.invoke. Nothing named the real problem.
     *
     * The class chain is searched first, all of it, before any interface: a class's
     * own body always beats an inherited declaration, which is the same precedence
     * DexClass::FindVirtualMethod applies on the invoke side.
     */
    public java.lang.reflect.Method getMethod(String name, Class<?>... parameterTypes)
            throws NoSuchMethodException {
        Class<?> c = this;
        while (c != null) {
            java.lang.reflect.Method[] all = c.getDeclaredMethods();
            for (int i = 0; i < all.length; i++) {
                if (all[i].getName().equals(name)
                        && matches(all[i].getParameterTypes(), parameterTypes)) {
                    return all[i];
                }
            }
            c = c.getSuperclass();
        }
        // Interfaces, walked transitively: a method may be declared on an interface
        // the directly-implemented one extends.
        java.lang.reflect.Method found = findInterfaceMethod(this, name, parameterTypes);
        if (found != null) return found;
        throw new NoSuchMethodException(getName() + "." + name);
    }

    /** Depth-first search of the interface graph reachable from `c`, or null. */
    private static java.lang.reflect.Method findInterfaceMethod(
            Class<?> c, String name, Class<?>[] parameterTypes) {
        while (c != null) {
            Class<?>[] ifaces = c.getInterfaces();
            for (int i = 0; i < ifaces.length; i++) {
                if (ifaces[i] == null) continue;
                java.lang.reflect.Method[] all = ifaces[i].getDeclaredMethods();
                for (int j = 0; j < all.length; j++) {
                    if (all[j].getName().equals(name)
                            && matches(all[j].getParameterTypes(), parameterTypes)) {
                        return all[j];
                    }
                }
                java.lang.reflect.Method deeper =
                        findInterfaceMethod(ifaces[i], name, parameterTypes);
                if (deeper != null) return deeper;
            }
            c = c.getSuperclass();
        }
        return null;
    }

    /**
     * Every public method of this class, its superclasses AND its interfaces.
     *
     * Callers enumerate this to decide whether a method exists at all — Unity's
     * JNIBridge does exactly that for the interfaces it proxies — so leaving the
     * interface methods out reports a Runnable proxy as having no run().
     */
    public java.lang.reflect.Method[] getMethods() {
        java.util.ArrayList<java.lang.reflect.Method> out =
                new java.util.ArrayList<java.lang.reflect.Method>();
        Class<?> c = this;
        while (c != null) {
            java.lang.reflect.Method[] all = c.getDeclaredMethods();
            for (int i = 0; i < all.length; i++) {
                out.add(all[i]);
            }
            c = c.getSuperclass();
        }
        collectInterfaceMethods(this, out);
        java.lang.reflect.Method[] arr = new java.lang.reflect.Method[out.size()];
        for (int i = 0; i < arr.length; i++) {
            arr[i] = out.get(i);
        }
        return arr;
    }

    /**
     * Append interface methods not already present, by name and parameter types.
     *
     * De-duplicated on the signature rather than on the Method object: a class that
     * implements an interface has its own Method for the same signature, and
     * returning both would make a caller that dispatches on getMethods() invoke the
     * same logic twice.
     */
    private static void collectInterfaceMethods(
            Class<?> c, java.util.ArrayList<java.lang.reflect.Method> out) {
        while (c != null) {
            Class<?>[] ifaces = c.getInterfaces();
            for (int i = 0; i < ifaces.length; i++) {
                if (ifaces[i] == null) continue;
                java.lang.reflect.Method[] all = ifaces[i].getDeclaredMethods();
                for (int j = 0; j < all.length; j++) {
                    boolean seen = false;
                    for (int k = 0; k < out.size(); k++) {
                        java.lang.reflect.Method m = out.get(k);
                        if (m.getName().equals(all[j].getName())
                                && matches(m.getParameterTypes(), all[j].getParameterTypes())) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) out.add(all[j]);
                }
                collectInterfaceMethods(ifaces[i], out);
            }
            c = c.getSuperclass();
        }
    }

    public java.lang.reflect.Field getDeclaredField(String name) throws NoSuchFieldException {
        java.lang.reflect.Field[] all = getDeclaredFields();
        for (int i = 0; i < all.length; i++) {
            if (all[i].getName().equals(name)) {
                return all[i];
            }
        }
        throw new NoSuchFieldException(getName() + "." + name);
    }

    public java.lang.reflect.Field getField(String name) throws NoSuchFieldException {
        Class<?> c = this;
        while (c != null) {
            java.lang.reflect.Field[] all = c.getDeclaredFields();
            for (int i = 0; i < all.length; i++) {
                if (all[i].getName().equals(name)) {
                    return all[i];
                }
            }
            c = c.getSuperclass();
        }
        throw new NoSuchFieldException(getName() + "." + name);
    }

    public java.lang.reflect.Field[] getFields() {
        return getDeclaredFields();
    }

    public java.lang.reflect.Constructor<T> getDeclaredConstructor(Class<?>... parameterTypes)
            throws NoSuchMethodException {
        java.lang.reflect.Constructor<T>[] all = getDeclaredConstructors();
        for (int i = 0; i < all.length; i++) {
            if (matches(all[i].getParameterTypes(), parameterTypes)) {
                return all[i];
            }
        }
        throw new NoSuchMethodException(getName() + ".<init>");
    }

    public java.lang.reflect.Constructor<T> getConstructor(Class<?>... parameterTypes)
            throws NoSuchMethodException {
        return getDeclaredConstructor(parameterTypes);
    }

    public java.lang.reflect.Constructor<T>[] getConstructors() {
        return getDeclaredConstructors();
    }

    public T[] getEnumConstants() {
        return null;
    }

    public java.io.InputStream getResourceAsStream(String name) {
        return null;
    }

    public String toString() {
        return (isInterface() ? "interface " : "class ") + getName();
    }

    private static boolean matches(Class<?>[] actual, Class<?>[] wanted) {
        if (wanted == null) {
            return actual.length == 0;
        }
        if (actual.length != wanted.length) {
            return false;
        }
        for (int i = 0; i < actual.length; i++) {
            if (actual[i] != wanted[i]) {
                return false;
            }
        }
        return true;
    }
}
