package java.io;

public class File implements Comparable<File> {

    public static final char separatorChar = '/';
    public static final String separator = "/";
    public static final char pathSeparatorChar = ':';
    public static final String pathSeparator = ":";

    private final String path;

    public File(String pathname) {
        this.path = normalize(pathname == null ? "" : pathname);
    }

    public File(String parent, String child) {
        this(join(parent, child));
    }

    public File(File parent, String child) {
        this(parent == null ? child : join(parent.path, child));
    }

    private static String join(String parent, String child) {
        if (parent == null || parent.length() == 0) {
            return child;
        }
        if (child == null || child.length() == 0) {
            return parent;
        }
        boolean parentSlash = parent.charAt(parent.length() - 1) == separatorChar;
        boolean childSlash = child.charAt(0) == separatorChar;
        if (parentSlash && childSlash) {
            return parent + child.substring(1);
        }
        if (!parentSlash && !childSlash) {
            return parent + separator + child;
        }
        return parent + child;
    }

    private static String normalize(String p) {
        // Bỏ dấu '/' thừa ở cuối, giữ nguyên "/" gốc.
        while (p.length() > 1 && p.charAt(p.length() - 1) == separatorChar) {
            p = p.substring(0, p.length() - 1);
        }
        return p;
    }

    public String getPath() {
        return path;
    }

    public String getAbsolutePath() {
        return isAbsolute() ? path : join(System.getProperty("user.dir"), path);
    }

    public File getAbsoluteFile() {
        return new File(getAbsolutePath());
    }

    public String getCanonicalPath() throws IOException {
        return getAbsolutePath();
    }

    public File getCanonicalFile() throws IOException {
        return getAbsoluteFile();
    }

    public String getName() {
        int idx = path.lastIndexOf(separatorChar);
        return idx < 0 ? path : path.substring(idx + 1);
    }

    public String getParent() {
        int idx = path.lastIndexOf(separatorChar);
        if (idx < 0) {
            return null;
        }
        return idx == 0 ? separator : path.substring(0, idx);
    }

    public File getParentFile() {
        String parent = getParent();
        return parent == null ? null : new File(parent);
    }

    public boolean isAbsolute() {
        return path.length() > 0 && path.charAt(0) == separatorChar;
    }

    public native boolean exists();

    public native boolean isDirectory();

    public native boolean isFile();

    public native boolean canRead();

    public native boolean canWrite();

    public native long length();

    public native long lastModified();

    public native boolean delete();

    public native boolean mkdir();

    public native boolean createNewFile() throws IOException;

    public native boolean renameTo(File dest);

    public native String[] list();

    public boolean isHidden() {
        return getName().startsWith(".");
    }

    public boolean canExecute() {
        return canRead();
    }

    public boolean setReadable(boolean readable) {
        return false;
    }

    public boolean setWritable(boolean writable) {
        return false;
    }

    public boolean setExecutable(boolean executable) {
        return false;
    }

    public boolean setLastModified(long time) {
        return false;
    }

    public boolean mkdirs() {
        if (exists()) {
            return false;
        }
        File parent = getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }
        return mkdir();
    }

    public void deleteOnExit() {
    }

    public File[] listFiles() {
        String[] names = list();
        if (names == null) {
            return null;
        }
        File[] out = new File[names.length];
        for (int i = 0; i < names.length; i++) {
            out[i] = new File(this, names[i]);
        }
        return out;
    }

    public File[] listFiles(FileFilter filter) {
        File[] all = listFiles();
        if (all == null) {
            return null;
        }
        java.util.ArrayList<File> kept = new java.util.ArrayList<File>();
        for (int i = 0; i < all.length; i++) {
            if (filter == null || filter.accept(all[i])) {
                kept.add(all[i]);
            }
        }
        File[] out = new File[kept.size()];
        for (int i = 0; i < out.length; i++) {
            out[i] = kept.get(i);
        }
        return out;
    }

    public long getFreeSpace() {
        return 0;
    }

    public long getTotalSpace() {
        return 0;
    }

    public long getUsableSpace() {
        return 0;
    }

    public int compareTo(File other) {
        return path.compareTo(other.path);
    }

    public boolean equals(Object other) {
        return (other instanceof File) && ((File) other).path.equals(path);
    }

    public int hashCode() {
        return path.hashCode();
    }

    public String toString() {
        return path;
    }
}
