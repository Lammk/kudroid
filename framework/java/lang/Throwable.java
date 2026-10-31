package java.lang;

import java.io.PrintStream;
import java.io.PrintWriter;

public class Throwable {

    private String message;
    private Throwable cause;
    private boolean causeInitialized;
    // long[] of (method, pc) pairs recorded by the VM; null when none was recorded.
    private transient Object backtrace;
    // Materialised from backtrace on first use.
    private StackTraceElement[] stackTrace;
    // The flags are inverted so an exception created by native code (all fields zero,
    // no constructor run) behaves as writable with suppression enabled.
    private boolean stackTraceFrozen;
    private boolean suppressionDisabled;
    private java.util.ArrayList<Throwable> suppressed;

    public Throwable() {
        fillInStackTrace();
    }

    public Throwable(String message) {
        this.message = message;
        fillInStackTrace();
    }

    public Throwable(String message, Throwable cause) {
        this.message = message;
        this.cause = cause;
        this.causeInitialized = true;
        fillInStackTrace();
    }

    public Throwable(Throwable cause) {
        this.cause = cause;
        this.causeInitialized = true;
        this.message = cause == null ? null : cause.toString();
        fillInStackTrace();
    }

    protected Throwable(String message, Throwable cause, boolean enableSuppression,
            boolean writableStackTrace) {
        this.message = message;
        this.cause = cause;
        this.causeInitialized = true;
        this.suppressionDisabled = !enableSuppression;
        if (writableStackTrace) {
            fillInStackTrace();
        } else {
            this.stackTraceFrozen = true;
        }
    }

    public String getMessage() {
        return message;
    }

    public String getLocalizedMessage() {
        return getMessage();
    }

    public synchronized Throwable getCause() {
        return cause;
    }

    public synchronized Throwable initCause(Throwable cause) {
        if (causeInitialized) throw new IllegalStateException("Cause already initialized");
        if (cause == this) throw new IllegalArgumentException("Self-causation not permitted");
        this.cause = cause;
        this.causeInitialized = true;
        return this;
    }

    private StackTraceElement[] getOurStackTrace() {
        StackTraceElement[] trace = stackTrace;
        if (trace == null) {
            if (backtrace != null) trace = nativeGetStackTrace(backtrace);
            if (trace == null) trace = new StackTraceElement[0];
            stackTrace = trace;
        }
        return trace;
    }

    public StackTraceElement[] getStackTrace() {
        StackTraceElement[] trace = getOurStackTrace();
        StackTraceElement[] copy = new StackTraceElement[trace.length];
        System.arraycopy(trace, 0, copy, 0, trace.length);
        return copy;
    }

    public void setStackTrace(StackTraceElement[] trace) {
        if (trace == null) throw new NullPointerException("stackTrace == null");
        StackTraceElement[] copy = new StackTraceElement[trace.length];
        for (int i = 0; i < trace.length; i++) {
            if (trace[i] == null) throw new NullPointerException("stackTrace[" + i + "] == null");
            copy[i] = trace[i];
        }
        if (stackTraceFrozen) return;
        this.stackTrace = copy;
    }

    public Throwable fillInStackTrace() {
        if (!stackTraceFrozen) {
            backtrace = nativeFillInStackTrace();
            stackTrace = null;
        }
        return this;
    }

    public final void addSuppressed(Throwable exception) {
        if (exception == this) throw new IllegalArgumentException("Self-suppression not permitted");
        if (exception == null) throw new NullPointerException("Cannot suppress a null exception.");
        if (suppressionDisabled) return;
        if (suppressed == null) suppressed = new java.util.ArrayList<Throwable>(1);
        suppressed.add(exception);
    }

    public final Throwable[] getSuppressed() {
        if (suppressed == null) return new Throwable[0];
        return suppressed.toArray(new Throwable[0]);
    }

    public void printStackTrace() {
        printStackTrace(System.err);
    }

    // The whole trace goes out as one print so it stays one log record.
    public void printStackTrace(PrintStream s) {
        s.println(stackTraceText());
    }

    public void printStackTrace(PrintWriter s) {
        s.println(stackTraceText());
        s.flush();
    }

    private String stackTraceText() {
        StringBuilder sb = new StringBuilder(256);
        java.util.ArrayList<Throwable> seen = new java.util.ArrayList<Throwable>();
        StackTraceElement[] enclosing = null;
        String caption = "";
        for (Throwable t = this; t != null; t = t.getCause()) {
            boolean circular = false;
            for (int i = 0; i < seen.size(); i++) {
                if (seen.get(i) == t) circular = true;
            }
            if (circular) {
                sb.append("\n\t[CIRCULAR REFERENCE: ").append(t.toString()).append("]");
                break;
            }
            seen.add(t);
            StackTraceElement[] trace = t.getOurStackTrace();
            int m = trace.length - 1;
            if (enclosing != null) {
                int n = enclosing.length - 1;
                while (m >= 0 && n >= 0 && trace[m].equals(enclosing[n])) {
                    m--;
                    n--;
                }
            }
            int common = trace.length - 1 - m;
            sb.append(caption).append(t.toString());
            for (int i = 0; i <= m; i++) {
                sb.append("\n\tat ").append(trace[i]);
            }
            if (common != 0) sb.append("\n\t... ").append(common).append(" more");
            if (t.suppressed != null) {
                for (int i = 0; i < t.suppressed.size(); i++) {
                    Throwable s = t.suppressed.get(i);
                    sb.append("\n\tSuppressed: ").append(s.toString());
                    StackTraceElement[] st = s.getOurStackTrace();
                    for (int j = 0; j < st.length; j++) {
                        sb.append("\n\t\tat ").append(st[j]);
                    }
                }
            }
            enclosing = trace;
            caption = "\nCaused by: ";
        }
        return sb.toString();
    }

    public String toString() {
        String name = getClass().getName();
        String text = getLocalizedMessage();
        return text == null ? name : name + ": " + text;
    }

    private static native Object nativeFillInStackTrace();

    private static native StackTraceElement[] nativeGetStackTrace(Object backtrace);
}
