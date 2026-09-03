package java.lang.invoke;

/**
 * Factory for MethodHandles.
 *
 * Lookup is what apps actually reach for, and the shape they use is narrow: take a
 * java.lang.reflect.Method and turn it into a handle, usually via unreflectSpecial so an
 * interface default method can be invoked non-virtually.
 *
 * The lookup class and access mode are recorded but not enforced. There is no module system
 * and no security manager in KuART, so every member is already reachable — refusing a
 * lookup would report a restriction that does not exist, and callers respond to a refusal
 * by taking a fallback path that also does not work here.
 *
 * The two-argument constructor is deliberately present and accessible. On the platform it is
 * private, and the standard way to obtain a Lookup with private access to an arbitrary class
 * is to reflect on it:
 *
 *   Constructor c = Lookup.class.getDeclaredConstructor(Class.class, int.class);
 *   c.setAccessible(true);
 *   Lookup lookup = (Lookup) c.newInstance(someClass, Lookup.PRIVATE);
 *
 * Any library doing this needs getDeclaredConstructor to MATCH on (Class, int) — which is
 * why the primitive class literals had to be fixed first: int.class was null, so the
 * parameter comparison could never succeed and this constructor was unreachable.
 */
public class MethodHandles {

    public static final class Lookup {
        public static final int PUBLIC = 0x0001;
        public static final int PRIVATE = 0x0002;
        public static final int PROTECTED = 0x0004;
        public static final int PACKAGE = 0x0008;

        private final Class<?> lookupClass;
        private final int allowedModes;

        public Lookup(Class<?> lookupClass, int allowedModes) {
            this.lookupClass = lookupClass;
            this.allowedModes = allowedModes;
        }

        Lookup(Class<?> lookupClass) {
            this(lookupClass, PUBLIC | PRIVATE | PROTECTED | PACKAGE);
        }

        public Class<?> lookupClass() {
            return lookupClass;
        }

        public int lookupModes() {
            return allowedModes;
        }

        /** A Lookup on a different class, keeping the same modes. */
        public Lookup in(Class<?> requestedLookupClass) {
            return new Lookup(requestedLookupClass, allowedModes);
        }

        /**
         * A handle that invokes `m` the way an ordinary call would: virtually for an
         * instance method, so an override on the receiver wins.
         */
        public native MethodHandle unreflect(java.lang.reflect.Method m)
                throws IllegalAccessException;

        /**
         * A handle that invokes `m` itself, bypassing virtual dispatch.
         *
         * This is the only way to run an interface default method on an object whose class
         * routes that method somewhere else — a Proxy, above all. unreflect() on the same
         * Method would dispatch back to the proxy's InvocationHandler and recurse until the
         * stack ran out.
         *
         * `specialCaller` is accepted and ignored beyond an access check that does not
         * apply here; the platform uses it to verify the caller may make a super-call.
         */
        public native MethodHandle unreflectSpecial(java.lang.reflect.Method m,
                                                   Class<?> specialCaller)
                throws IllegalAccessException;

        public native MethodHandle unreflectConstructor(java.lang.reflect.Constructor<?> c)
                throws IllegalAccessException;

        /** Virtual lookup by name and type; the receiver is a leading parameter. */
        public native MethodHandle findVirtual(Class<?> refc, String name, MethodType type)
                throws NoSuchMethodException, IllegalAccessException;

        public native MethodHandle findStatic(Class<?> refc, String name, MethodType type)
                throws NoSuchMethodException, IllegalAccessException;

        /** Non-virtual lookup, the findX form of unreflectSpecial. */
        public native MethodHandle findSpecial(Class<?> refc, String name, MethodType type,
                                               Class<?> specialCaller)
                throws NoSuchMethodException, IllegalAccessException;

        public native MethodHandle findConstructor(Class<?> refc, MethodType type)
                throws NoSuchMethodException, IllegalAccessException;

        @Override
        public String toString() {
            return "Lookup[" + (lookupClass == null ? "?" : lookupClass.getName()) + "]";
        }
    }

    /**
     * A Lookup on the CALLER's class, with full access.
     *
     * The platform derives the lookup class from the call stack. KuART's interpreter knows
     * its caller, so the native implementation reports the real one rather than a
     * placeholder — a Lookup whose lookupClass() lied would break libraries that pass it to
     * in() or compare it.
     */
    public static native Lookup lookup();

    public static Lookup publicLookup() {
        return new Lookup(Object.class, Lookup.PUBLIC);
    }

    /** Reflects `m` through `lookup`, matching the platform's convenience form. */
    public static <T extends java.lang.reflect.AccessibleObject> T reflectAs(Class<T> expected,
                                                                            MethodHandle target) {
        throw new UnsupportedOperationException("reflectAs");
    }
}
