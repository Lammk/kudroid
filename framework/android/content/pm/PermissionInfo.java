package android.content.pm;

public class PermissionInfo {
    public static final int PROTECTION_NORMAL = 0;
    public static final int PROTECTION_DANGEROUS = 1;
    public static final int PROTECTION_SIGNATURE = 2;
    public static final int PROTECTION_SIGNATURE_OR_SYSTEM = 3;
    public static final int PROTECTION_MASK_BASE = 0xf;
    public static final int PROTECTION_FLAG_PRIVILEGED = 0x10;
    public static final int PROTECTION_FLAG_SYSTEM = 0x10;
    public static final int PROTECTION_FLAG_DEVELOPMENT = 0x20;
    public static final int PROTECTION_FLAG_APPOP = 0x40;
    public static final int PROTECTION_FLAG_PRE23 = 0x80;
    public static final int PROTECTION_FLAG_INSTALLER = 0x100;
    public static final int PROTECTION_FLAG_VERIFIER = 0x200;
    public static final int PROTECTION_FLAG_PREINSTALLED = 0x400;
    public static final int PROTECTION_FLAG_SETUP = 0x800;
    public static final int PROTECTION_FLAG_INSTANT = 0x1000;
    public static final int PROTECTION_FLAG_RUNTIME_ONLY = 0x2000;
    public static final int PROTECTION_MASK_FLAGS = 0xff0;

    public String name;
    public String packageName;
    public String group;
    public int protectionLevel;
    public int flags;

    public PermissionInfo() {}

    public int getProtection() {
        return protectionLevel & PROTECTION_MASK_BASE;
    }

    public int getProtectionFlags() {
        return protectionLevel & ~PROTECTION_MASK_BASE;
    }
}
