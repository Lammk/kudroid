package java.util.logging;

import java.io.IOException;

public class FileHandler extends StreamHandler {
    public FileHandler() throws IOException, SecurityException {}
    public FileHandler(String pattern) throws IOException, SecurityException {}
    public FileHandler(String pattern, boolean append) throws IOException, SecurityException {}
    public FileHandler(String pattern, int limit, int count) throws IOException, SecurityException {}
    public FileHandler(String pattern, int limit, int count, boolean append) throws IOException, SecurityException {}
}
