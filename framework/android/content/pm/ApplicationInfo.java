package android.content.pm;

/**
 * triển khai android.content.pm.applicationinfo tối thiểu.
 *
 * mô tả một ứng dụng. đối với khuôn khổ tối thiểu của kudroid, cung cấp các trường
 * cơ bản với các giá trị mặc định.
 */
public class ApplicationInfo {
    /** tên gói của ứng dụng này. */
    public String packageName = "";
    /** đường dẫn đầy đủ đến apk cơ sở cho ứng dụng này. */
    public String sourceDir = "";
    /** đường dẫn đầy đủ đến thư mục nơi apk được cài đặt. */
    public String dataDir = "";
    /** đường dẫn đầy đủ đến thư mục chứa các thư viện gốc. */
    public String nativeLibraryDir = "";
    /** phiên bản sdk tối thiểu bắt buộc. */
    public int minSdkVersion = 0;
    /** phiên bản sdk mục tiêu. */
    public int targetSdkVersion = 0;
    /** nhãn của ứng dụng. */
    public CharSequence loadLabel = "";

    public ApplicationInfo() {
    }

    public ApplicationInfo(ApplicationInfo orig) {
        packageName = orig.packageName;
        sourceDir = orig.sourceDir;
        dataDir = orig.dataDir;
        nativeLibraryDir = orig.nativeLibraryDir;
        minSdkVersion = orig.minSdkVersion;
        targetSdkVersion = orig.targetSdkVersion;
        loadLabel = orig.loadLabel;
    }
}
