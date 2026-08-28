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
        throw new NoSuchMethodException(getName() + "." + name);
    }

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
        java.lang.reflect.Method[] arr = new java.lang.reflect.Method[out.size()];
        for (int i = 0; i < arr.length; i++) {
            arr[i] = out.get(i);
        }
        return arr;
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
