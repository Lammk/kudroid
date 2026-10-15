package java.lang;

/**
 * The placeholder class for the void type.
 *
 * Void carries no values and cannot be instantiated; the only thing on it that matters is
 * TYPE. javac compiles {@code void.class} into {@code getstatic java/lang/Void.TYPE}, so a
 * missing Void made every {@code void.class} in guest bytecode unresolvable — and
 * {@code void.class} is not exotic: it is how a reflective call states that a method
 * returns nothing, which is what MethodType.methodType(void.class, ...) and every
 * signature-matching helper does.
 *
 * The class must exist even though no code ever creates one, because a class literal
 * needs a class to read the field from.
 */
public final class Void {

    // void.class reads this field. Null here would make the literal null, which then
    // silently fails every reference-identity comparison against a real return type.
    @SuppressWarnings("unchecked")
    public static final Class<Void> TYPE = (Class<Void>) Class.getPrimitiveClass("void");

    // Uninstantiable, matching the platform: there is no void value to wrap.
    private Void() {
    }
}
