import java.io.PrintWriter;
import java.io.StringWriter;

/** Throwable stack traces: capture site, subclasses, flags, printing and Thread.getStackTrace. */
public final class ThrowableTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run() {
        TestSupport.report.setLength(0);
        try { captureSite(); } catch (Throwable t) { TestSupport.fail("captureSite", t); }
        try { subclassAndRethrow(); } catch (Throwable t) { TestSupport.fail("subclassAndRethrow", t); }
        try { nativeOrigin(); } catch (Throwable t) { TestSupport.fail("nativeOrigin", t); }
        try { flags(); } catch (Throwable t) { TestSupport.fail("flags", t); }
        try { printing(); } catch (Throwable t) { TestSupport.fail("printing", t); }
        try { threadTrace(); } catch (Throwable t) { TestSupport.fail("threadTrace", t); }
        try { depthCap(); } catch (Throwable t) { TestSupport.fail("depthCap", t); }
        return TestSupport.report.toString();
    }

    static Throwable a() { return new Throwable("made in a"); }
    static Throwable b() { return a(); }

    static void captureSite() {
        StackTraceElement[] frames = b().getStackTrace();
        check(frames.length >= 3, "a trace is recorded");
        check(frames.length > 0 && frames[0].getMethodName().equals("a"),
                "frame 0 is the creating method, not the constructor chain");
        check(frames.length > 1 && frames[1].getMethodName().equals("b"), "frame 1 is the caller");
        check(frames.length > 0 && "ThrowableTests.java".equals(frames[0].getFileName()),
                "the source file is known");
        check(frames.length > 0 && frames[0].getLineNumber() > 0, "the line number is known");
        check(frames.length > 0 && frames[0].getClassName().equals("ThrowableTests"),
                "the class name is known");

        StackTraceElement[] copy = b().getStackTrace();
        copy[0] = null;
        Throwable t = b();
        check(t.getStackTrace()[0] != null, "getStackTrace returns a copy");
        check(t.getStackTrace()[0].equals(t.getStackTrace()[0]), "elements compare equal");
    }

    static class MyEx extends RuntimeException {
        MyEx(String m) { super(m); }
    }

    static MyEx make() { return new MyEx("subclass"); }

    static void thrower(Throwable t) throws Throwable { throw t; }

    static void subclassAndRethrow() {
        StackTraceElement[] frames = make().getStackTrace();
        check(frames.length > 0 && frames[0].getMethodName().equals("make"),
                "a subclass trace starts at its creator, not <init>");
        Throwable origin = a();
        try {
            try { thrower(origin); } catch (Throwable caught) { thrower(caught); }
        } catch (Throwable rethrown) {
            check(rethrown == origin, "the same object is rethrown");
            check(rethrown.getStackTrace()[0].getMethodName().equals("a"),
                    "a rethrown exception keeps the place it was created");
        }
    }

    static void nativeOrigin() {
        try {
            "abc".charAt(9);
            check(false, "charAt(9) throws");
        } catch (StringIndexOutOfBoundsException e) {
            StackTraceElement[] frames = e.getStackTrace();
            check(frames.length > 0 && frames[0].getMethodName().equals("nativeOrigin"),
                    "an exception thrown by native code has the Java caller as frame 0");
        }
        try {
            int[] none = new int[0];
            none[1] = 1;
        } catch (ArrayIndexOutOfBoundsException e) {
            StackTraceElement[] frames = e.getStackTrace();
            check(frames.length > 0 && frames[0].getMethodName().equals("nativeOrigin"),
                    "an interpreter-raised exception is located too");
        }
    }

    static class NoTrace extends Exception {
        NoTrace() { super("no trace"); }
        public Throwable fillInStackTrace() { return this; }
    }

    static class Frozen extends Exception {
        Frozen(boolean suppression) { super("frozen", null, suppression, false); }
    }

    static void flags() {
        check(new NoTrace().getStackTrace().length == 0, "an overridden fillInStackTrace captures nothing");
        Frozen frozen = new Frozen(false);
        check(frozen.getStackTrace().length == 0, "writableStackTrace=false captures nothing");
        frozen.setStackTrace(new StackTraceElement[] {new StackTraceElement("X", "y", "X.java", 1)});
        check(frozen.getStackTrace().length == 0, "and ignores setStackTrace");
        frozen.addSuppressed(new RuntimeException("dropped"));
        check(frozen.getSuppressed().length == 0, "enableSuppression=false drops suppressed exceptions");

        Throwable t = a();
        t.addSuppressed(new RuntimeException("kept"));
        check(t.getSuppressed().length == 1, "suppressed exceptions are recorded");
        t.setStackTrace(new StackTraceElement[] {new StackTraceElement("Custom", "frame", "Custom.java", 7)});
        check(t.getStackTrace().length == 1 && t.getStackTrace()[0].getMethodName().equals("frame"),
                "setStackTrace replaces the trace");
        StackTraceElement synthetic = new StackTraceElement("N", "m", "N.java", -2);
        check(synthetic.isNativeMethod() && synthetic.toString().equals("N.m(Native Method)"),
                "a native frame prints as Native Method");
        check(new StackTraceElement("N", "m", null, -1).toString().equals("N.m(Unknown Source)"),
                "no file prints as Unknown Source");
    }

    static void printing() {
        Throwable root = a();
        Throwable top = new RuntimeException("wrapper", root);
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        top.printStackTrace(pw);
        pw.flush();
        String text = sw.toString();
        check(text.startsWith("java.lang.RuntimeException: wrapper"), "the trace begins with toString()");
        check(text.contains("\tat ThrowableTests.printing(ThrowableTests.java:"), "frames are printed");
        check(text.contains("Caused by: java.lang.Throwable: made in a"), "the cause chain is printed");
        check(text.contains("\tat ThrowableTests.a(ThrowableTests.java:"), "cause frames are printed");
        check(text.contains("more"), "frames shared with the enclosing trace are elided");

        String logged = android.util.Log.getStackTraceString(top);
        check(logged.contains("ThrowableTests.a(") && logged.contains("wrapper"),
                "Log.getStackTraceString includes the frames and the cause");
        check(android.util.Log.getStackTraceString(new java.net.UnknownHostException("h")).isEmpty(),
                "an UnknownHostException trace is suppressed like Android does");
        check(android.util.Log.getStackTraceString(null).isEmpty(), "null prints nothing");
    }

    static void threadTrace() {
        StackTraceElement[] frames = Thread.currentThread().getStackTrace();
        check(frames.length >= 3 && frames[0].getClassName().equals("dalvik.system.VMStack"),
                "frame 0 is VMStack.getThreadStackTrace");
        check(frames.length >= 3 && frames[1].getMethodName().equals("getStackTrace"),
                "frame 1 is Thread.getStackTrace");
        check(frames.length >= 3 && frames[2].getMethodName().equals("threadTrace"),
                "frame 2 is the caller");
    }

    static Throwable deep(int n) { return n == 0 ? new Throwable() : deep(n - 1); }

    static void depthCap() {
        StackTraceElement[] frames = deep(200).getStackTrace();
        check(frames.length == 64, "depth is capped at 64 frames (got " + frames.length + ")");
        check(frames.length > 0 && frames[0].getMethodName().equals("deep"), "the innermost frame is first");
    }
}
