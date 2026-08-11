package android.content.pm;

/**
 * triển khai android.content.pm.packagemanager tối thiểu.
 *
 * cung cấp quyền truy cập vào thông tin gói. đối với khuôn khổ tối thiểu của kudroid,
 * hầu hết các tra cứu đều trả về null/mặc định.
 */
public class PackageManager {
    /** quyền được cấp. */
    public static final int PERMISSION_GRANTED = 0;
    /** quyền bị từ chối. */
    public static final int PERMISSION_DENIED = -1;

    public PackageManager() {
    }

    /**
     * kiểm tra xem ứng dụng có quyền hay không. hiện tại trả về đã cấp quyền.
     */
    public int checkPermission(String permName, String pkgName) {
        return PERMISSION_GRANTED;
    }

    /**
     * trả về thông tin ứng dụng cho một gói. hiện tại trả về null.
     */
    public ApplicationInfo getApplicationInfo(String packageName, int flags) {
        return new ApplicationInfo();
    }

    /**
     * trả về thông tin gói cho một gói. hiện tại trả về null.
     */
    public PackageInfo getPackageInfo(String packageName, int flags) {
        return new PackageInfo();
    }

    /**
     * trả về tên gói đã cài đặt ứng dụng này.
     */
    public String getInstallerPackageName(String packageName) {
        return null;
    }
}
