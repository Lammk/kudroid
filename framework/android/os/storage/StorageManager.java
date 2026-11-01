package android.os.storage;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.UUID;

public class StorageManager {
    public static final UUID UUID_DEFAULT = UUID.fromString("41217664-9172-527a-b3d5-edabb50a7d69");

    public UUID getUuidForPath(File path) throws IOException {
        if (path == null) throw new NullPointerException("path == null");
        // Collapse path components before matching an emulated volume.
        String[] parts = path.getCanonicalPath().split("/");
        java.util.ArrayList<String> normalized = new java.util.ArrayList<String>();
        for (String part : parts) {
            if (part.length() == 0 || part.equals(".")) continue;
            if (part.equals("..")) {
                if (!normalized.isEmpty()) normalized.remove(normalized.size() - 1);
            } else normalized.add(part);
        }
        String canonical = "";
        for (String part : normalized) canonical += "/" + part;
        if (contains("/data", canonical) || contains("/sdcard", canonical)
                || contains("/storage/emulated/0", canonical) || contains("/mnt/sdcard", canonical)
                || contains("/storage/self/primary", canonical)) return UUID_DEFAULT;
        throw new FileNotFoundException("No storage volume for " + path);
    }

    private static boolean contains(String root, String path) {
        return path.equals(root) || path.startsWith(root + "/");
    }
}
