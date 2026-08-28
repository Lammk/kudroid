package android.os;

import java.io.*;

public final class FileUtils {
    public static final int S_IRWXU = 00700;
    public static final int S_IRWXG = 00070;
    public static final int S_IXOTH = 00001;

    public static boolean sync(FileOutputStream stream) {
        try {
            if (stream != null) stream.flush();
            return true;
        } catch (Exception e) {
            return false;
        }
    }
    public static int setPermissions(String path, int mode, int uid, int gid) {
        return 0;
    }
}
