package java.lang;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Describes a command to run; starting it fails because the host cannot spawn processes. */
public final class ProcessBuilder {
    private List<String> command;
    private File directory;
    private final Map<String, String> environment = new HashMap<String, String>();
    private boolean redirectErrorStream;

    public ProcessBuilder(List<String> command) {
        if (command == null) throw new NullPointerException();
        this.command = command;
    }

    public ProcessBuilder(String... command) {
        this.command = new ArrayList<String>(Arrays.asList(command));
    }

    public ProcessBuilder command(List<String> command) {
        if (command == null) throw new NullPointerException();
        this.command = command;
        return this;
    }

    public ProcessBuilder command(String... command) {
        this.command = new ArrayList<String>(Arrays.asList(command));
        return this;
    }

    public List<String> command() {
        return command;
    }

    public ProcessBuilder directory(File directory) {
        this.directory = directory;
        return this;
    }

    public File directory() {
        return directory;
    }

    public Map<String, String> environment() {
        return environment;
    }

    public ProcessBuilder redirectErrorStream(boolean redirectErrorStream) {
        this.redirectErrorStream = redirectErrorStream;
        return this;
    }

    public boolean redirectErrorStream() {
        return redirectErrorStream;
    }

    public Process start() throws IOException {
        if (command.isEmpty()) throw new IndexOutOfBoundsException();
        throw new IOException("Cannot run program \"" + command.get(0)
                + "\": error=2, No such file or directory");
    }
}
