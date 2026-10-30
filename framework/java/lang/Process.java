package java.lang;

import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.TimeUnit;

/** A native process. KuDroid cannot start one, so instances only exist as subclasses. */
public abstract class Process {
    public Process() {}

    public abstract OutputStream getOutputStream();

    public abstract InputStream getInputStream();

    public abstract InputStream getErrorStream();

    public abstract int waitFor() throws InterruptedException;

    public boolean waitFor(long timeout, TimeUnit unit) throws InterruptedException {
        waitFor();
        return true;
    }

    public abstract int exitValue();

    public abstract void destroy();

    public Process destroyForcibly() {
        destroy();
        return this;
    }

    public boolean isAlive() {
        try {
            exitValue();
            return false;
        } catch (IllegalThreadStateException e) {
            return true;
        }
    }
}
