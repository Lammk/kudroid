package java.lang.invoke;

public class MethodHandles {
    public static final class Lookup {
        public static final int PUBLIC = 0x0001;
        public static final int PRIVATE = 0x0002;
        public static final int PROTECTED = 0x0004;
        public static final int PACKAGE = 0x0008;
    }
    public static Lookup lookup() { return new Lookup(); }
    public static Lookup publicLookup() { return new Lookup(); }
}
