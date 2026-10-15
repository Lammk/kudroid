package java.lang.invoke;

/**
 * The type of a MethodHandle: a return type plus parameter types.
 *
 * The descriptor is the authoritative representation and the Class objects are derived
 * from it, not the other way round. A MethodType is normally built from class literals
 * ({@code methodType(void.class, int.class)}), and turning those straight into a DEX
 * descriptor once means the native invoke path has a signature to match against without
 * re-deriving it per call.
 *
 * Instances are immutable and compared by structure, because that is how callers use them:
 * a handle's type is checked against a requested type with equals(), and two independently
 * built descriptions of the same signature must be equal or every such check fails.
 */
public final class MethodType {

    private final Class<?> rtype;
    private final Class<?>[] ptypes;

    private MethodType(Class<?> rtype, Class<?>[] ptypes) {
        this.rtype = rtype;
        this.ptypes = ptypes == null ? new Class<?>[0] : ptypes;
    }

    public static MethodType methodType(Class<?> rtype, Class<?>[] ptypes) {
        // Copied rather than retained: the caller's array is theirs to mutate, and a
        // MethodType that changed underneath a handle would make its signature a lie.
        Class<?>[] copy;
        if (ptypes == null) {
            copy = new Class<?>[0];
        } else {
            copy = new Class<?>[ptypes.length];
            for (int i = 0; i < ptypes.length; i++) {
                copy[i] = ptypes[i];
            }
        }
        return new MethodType(rtype, copy);
    }

    public static MethodType methodType(Class<?> rtype) {
        return new MethodType(rtype, new Class<?>[0]);
    }

    public static MethodType methodType(Class<?> rtype, Class<?> ptype0) {
        return new MethodType(rtype, new Class<?>[] { ptype0 });
    }

    public static MethodType methodType(Class<?> rtype, Class<?> ptype0, Class<?>... rest) {
        int extra = rest == null ? 0 : rest.length;
        Class<?>[] all = new Class<?>[1 + extra];
        all[0] = ptype0;
        for (int i = 0; i < extra; i++) {
            all[1 + i] = rest[i];
        }
        return new MethodType(rtype, all);
    }

    public static MethodType methodType(Class<?> rtype, MethodType other) {
        return new MethodType(rtype, other == null ? new Class<?>[0] : other.ptypes);
    }

    /** The descriptor a DEX signature would use: "(ILjava/lang/String;)V". */
    public static MethodType fromMethodDescriptorString(String descriptor, ClassLoader loader) {
        if (descriptor == null || descriptor.length() < 3 || descriptor.charAt(0) != '(') {
            throw new IllegalArgumentException("not a method descriptor: " + descriptor);
        }
        int close = descriptor.indexOf(')');
        if (close < 0) {
            throw new IllegalArgumentException("not a method descriptor: " + descriptor);
        }
        java.util.ArrayList<Class<?>> params = new java.util.ArrayList<Class<?>>();
        int i = 1;
        while (i < close) {
            int end = endOfDescriptor(descriptor, i, close);
            params.add(classForDescriptor(descriptor.substring(i, end)));
            i = end;
        }
        Class<?>[] arr = new Class<?>[params.size()];
        for (int k = 0; k < arr.length; k++) {
            arr[k] = params.get(k);
        }
        return new MethodType(classForDescriptor(descriptor.substring(close + 1)), arr);
    }

    private static int endOfDescriptor(String s, int start, int limit) {
        int i = start;
        while (i < limit && s.charAt(i) == '[') {
            i++;
        }
        if (i < limit && s.charAt(i) == 'L') {
            int semi = s.indexOf(';', i);
            return semi < 0 ? limit : semi + 1;
        }
        return i + 1;
    }

    private static Class<?> classForDescriptor(String d) {
        if (d == null || d.isEmpty()) {
            return null;
        }
        switch (d.charAt(0)) {
            case 'I': return Integer.TYPE;
            case 'J': return Long.TYPE;
            case 'S': return Short.TYPE;
            case 'B': return Byte.TYPE;
            case 'C': return Character.TYPE;
            case 'F': return Float.TYPE;
            case 'D': return Double.TYPE;
            case 'Z': return Boolean.TYPE;
            case 'V': return Void.TYPE;
            default: break;
        }
        try {
            // Class.forName wants dotted names for object types and keeps the bracket form
            // for arrays, which is exactly the split below.
            if (d.charAt(0) == '[') {
                return Class.forName(d.replace('/', '.'));
            }
            if (d.charAt(0) == 'L' && d.charAt(d.length() - 1) == ';') {
                return Class.forName(d.substring(1, d.length() - 1).replace('/', '.'));
            }
        } catch (ClassNotFoundException e) {
            // A type this runtime does not have. Null rather than an exception: a
            // MethodType is often built to describe a signature that is then matched by
            // descriptor, and failing here would reject a handle that works.
            return null;
        }
        return null;
    }

    public Class<?> returnType() {
        return rtype;
    }

    public Class<?>[] parameterArray() {
        Class<?>[] copy = new Class<?>[ptypes.length];
        for (int i = 0; i < ptypes.length; i++) {
            copy[i] = ptypes[i];
        }
        return copy;
    }

    public java.util.List<Class<?>> parameterList() {
        return java.util.Arrays.asList(parameterArray());
    }

    public int parameterCount() {
        return ptypes.length;
    }

    public Class<?> parameterType(int index) {
        if (index < 0 || index >= ptypes.length) {
            throw new IndexOutOfBoundsException("parameterType " + index);
        }
        return ptypes[index];
    }

    /** A new type with `rtype` as the return type and these parameters. */
    public MethodType changeReturnType(Class<?> nrtype) {
        return new MethodType(nrtype, ptypes);
    }

    public MethodType changeParameterType(int index, Class<?> ntype) {
        Class<?>[] copy = parameterArray();
        if (index >= 0 && index < copy.length) {
            copy[index] = ntype;
        }
        return new MethodType(rtype, copy);
    }

    /** Drops the leading parameter, as binding a receiver does. */
    public MethodType dropParameterTypes(int start, int end) {
        if (start < 0 || end > ptypes.length || start > end) {
            throw new IndexOutOfBoundsException("dropParameterTypes " + start + ".." + end);
        }
        Class<?>[] copy = new Class<?>[ptypes.length - (end - start)];
        int w = 0;
        for (int i = 0; i < ptypes.length; i++) {
            if (i >= start && i < end) {
                continue;
            }
            copy[w++] = ptypes[i];
        }
        return new MethodType(rtype, copy);
    }

    public MethodType insertParameterTypes(int index, Class<?>... more) {
        int extra = more == null ? 0 : more.length;
        Class<?>[] copy = new Class<?>[ptypes.length + extra];
        int w = 0;
        for (int i = 0; i < index && i < ptypes.length; i++) {
            copy[w++] = ptypes[i];
        }
        for (int i = 0; i < extra; i++) {
            copy[w++] = more[i];
        }
        for (int i = index; i < ptypes.length; i++) {
            copy[w++] = ptypes[i];
        }
        return new MethodType(rtype, copy);
    }

    /** Every reference type replaced by Object, which is what erasure produces. */
    public MethodType erase() {
        Class<?>[] copy = new Class<?>[ptypes.length];
        for (int i = 0; i < ptypes.length; i++) {
            copy[i] = isPrimitive(ptypes[i]) ? ptypes[i] : Object.class;
        }
        return new MethodType(isPrimitive(rtype) ? rtype : Object.class, copy);
    }

    public MethodType generic() {
        Class<?>[] copy = new Class<?>[ptypes.length];
        for (int i = 0; i < ptypes.length; i++) {
            copy[i] = Object.class;
        }
        return new MethodType(Object.class, copy);
    }

    private static boolean isPrimitive(Class<?> c) {
        return c != null && c.isPrimitive();
    }

    public String toMethodDescriptorString() {
        StringBuilder sb = new StringBuilder();
        sb.append('(');
        for (int i = 0; i < ptypes.length; i++) {
            sb.append(descriptorOf(ptypes[i]));
        }
        sb.append(')');
        sb.append(descriptorOf(rtype));
        return sb.toString();
    }

    private static String descriptorOf(Class<?> c) {
        // A null type means the signature mentioned something this runtime could not
        // resolve. "Ljava/lang/Object;" keeps the descriptor well-formed so it can still
        // be parsed and compared; the alternative is a string no parser accepts.
        if (c == null) {
            return "Ljava/lang/Object;";
        }
        if (c.isPrimitive()) {
            if (c == Integer.TYPE) return "I";
            if (c == Long.TYPE) return "J";
            if (c == Short.TYPE) return "S";
            if (c == Byte.TYPE) return "B";
            if (c == Character.TYPE) return "C";
            if (c == Float.TYPE) return "F";
            if (c == Double.TYPE) return "D";
            if (c == Boolean.TYPE) return "Z";
            return "V";
        }
        String name = c.getName();
        if (name.charAt(0) == '[') {
            return name.replace('.', '/');
        }
        return "L" + name.replace('.', '/') + ";";
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof MethodType)) {
            return false;
        }
        MethodType o = (MethodType) other;
        if (rtype != o.rtype || ptypes.length != o.ptypes.length) {
            return false;
        }
        for (int i = 0; i < ptypes.length; i++) {
            if (ptypes[i] != o.ptypes[i]) {
                return false;
            }
        }
        return true;
    }

    @Override
    public int hashCode() {
        int h = rtype == null ? 0 : rtype.hashCode();
        for (int i = 0; i < ptypes.length; i++) {
            h = h * 31 + (ptypes[i] == null ? 0 : ptypes[i].hashCode());
        }
        return h;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder("(");
        for (int i = 0; i < ptypes.length; i++) {
            if (i > 0) {
                sb.append(',');
            }
            sb.append(ptypes[i] == null ? "?" : ptypes[i].getSimpleName());
        }
        sb.append(')');
        sb.append(rtype == null ? "?" : rtype.getSimpleName());
        return sb.toString();
    }
}
