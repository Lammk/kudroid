package java.lang.reflect;

public final class Modifier {

    public static final int PUBLIC = 0x0001;
    public static final int PRIVATE = 0x0002;
    public static final int PROTECTED = 0x0004;
    public static final int STATIC = 0x0008;
    public static final int FINAL = 0x0010;
    public static final int SYNCHRONIZED = 0x0020;
    public static final int VOLATILE = 0x0040;
    public static final int TRANSIENT = 0x0080;
    public static final int NATIVE = 0x0100;
    public static final int INTERFACE = 0x0200;
    public static final int ABSTRACT = 0x0400;
    public static final int STRICT = 0x0800;

    private Modifier() {
    }

    public static boolean isPublic(int mod) {
        return (mod & PUBLIC) != 0;
    }

    public static boolean isPrivate(int mod) {
        return (mod & PRIVATE) != 0;
    }

    public static boolean isProtected(int mod) {
        return (mod & PROTECTED) != 0;
    }

    public static boolean isStatic(int mod) {
        return (mod & STATIC) != 0;
    }

    public static boolean isFinal(int mod) {
        return (mod & FINAL) != 0;
    }

    public static boolean isSynchronized(int mod) {
        return (mod & SYNCHRONIZED) != 0;
    }

    public static boolean isVolatile(int mod) {
        return (mod & VOLATILE) != 0;
    }

    public static boolean isTransient(int mod) {
        return (mod & TRANSIENT) != 0;
    }

    public static boolean isNative(int mod) {
        return (mod & NATIVE) != 0;
    }

    public static boolean isInterface(int mod) {
        return (mod & INTERFACE) != 0;
    }

    public static boolean isAbstract(int mod) {
        return (mod & ABSTRACT) != 0;
    }

    /**
     * The modifier bits that may legally appear on a method.
     *
     * Used to mask raw access flags before rendering (Method.toString) so
     * dex-only bits never leak into the platform string Unity parses.
     */
    public static int methodModifiers() {
        return PUBLIC | PRIVATE | PROTECTED | STATIC | FINAL | SYNCHRONIZED | NATIVE | STRICT
                | ABSTRACT;
    }

    public static int constructorModifiers() {
        return PUBLIC | PRIVATE | PROTECTED;
    }

    public static int fieldModifiers() {
        return PUBLIC | PRIVATE | PROTECTED | STATIC | FINAL | TRANSIENT | VOLATILE;
    }

    /**
     * Canonical modifier string, AOSP order and spelling ("public abstract", ...).
     *
     * Unity's JNIBridge parses Method.toString() output, so this must read exactly
     * like the platform's — a missing "abstract" or a reordered pair is a parse
     * failure there, not a cosmetic difference here.
     */
    public static String toString(int mod) {
        StringBuilder sb = new StringBuilder();
        if ((mod & PUBLIC) != 0) sb.append("public ");
        if ((mod & PROTECTED) != 0) sb.append("protected ");
        if ((mod & PRIVATE) != 0) sb.append("private ");
        if ((mod & ABSTRACT) != 0) sb.append("abstract ");
        if ((mod & STATIC) != 0) sb.append("static ");
        if ((mod & FINAL) != 0) sb.append("final ");
        if ((mod & TRANSIENT) != 0) sb.append("transient ");
        if ((mod & VOLATILE) != 0) sb.append("volatile ");
        if ((mod & SYNCHRONIZED) != 0) sb.append("synchronized ");
        if ((mod & NATIVE) != 0) sb.append("native ");
        if ((mod & STRICT) != 0) sb.append("strictfp ");
        if ((mod & INTERFACE) != 0) sb.append("interface ");
        int len = sb.length();
        if (len == 0) return "";
        return sb.toString().substring(0, len - 1);
    }
}
