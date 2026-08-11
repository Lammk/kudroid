package android.content.pm;

/**
 * triển khai android.content.pm.packageinfo tối thiểu.
 *
 * mô tả một gói. đối với khuôn khổ tối thiểu của kudroid, cung cấp các trường
 * cơ bản với các giá trị mặc định.
 */
public class PackageInfo {
    /** tên của gói. */
    public String packageName = "";
    /** mã phiên bản. */
    public int versionCode = 0;
    /** tên phiên bản. */
    public String versionName = "";
    /** thông tin ứng dụng. */
    public ApplicationInfo applicationInfo = new ApplicationInfo();

    public PackageInfo() {
    }
}
