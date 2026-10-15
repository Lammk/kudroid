/** Collects pass/fail lines that the native harness prints. */
public final class TestSupport {
    static final StringBuilder report = new StringBuilder();

    static String last = "(start)";

    static void check(boolean ok, String name) {
        last = name;
        report.append(ok ? "OK   " : "FAIL ").append(name).append('\n');
    }

    static void fail(String name, Throwable t) {
        report.append("FAIL ").append(name).append(" after \"").append(last).append("\": ").append(t);
        StackTraceElement[] frames = t.getStackTrace();
        for (int i = 0; frames != null && i < frames.length && i < 6; i++) {
            report.append(" <- ").append(frames[i]);
        }
        report.append('\n');
    }

    static String read(java.io.InputStream in) throws java.io.IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[4096];
        int n;
        while ((n = in.read(buf, 0, buf.length)) >= 0) out.write(buf, 0, n);
        return new String(out.toByteArray());
    }
}
