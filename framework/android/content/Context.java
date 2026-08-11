package android.content;

import android.os.Bundle;

/**
 * triển khai android.content.context tối thiểu.
 *
 * cung cấp quyền truy cập vào các tài nguyên ứng dụng, tùy chọn chia sẻ và các dịch vụ
 * hệ thống khác. đối với khuôn khổ tối thiểu của kudroid, hầu hết các phương thức trả về
 * giá trị mặc định hoặc null để ứng dụng không gặp sự cố trong quá trình khởi động.
 */
public abstract class Context {
    /** chế độ tệp: world-readable. */
    public static final int MODE_WORLD_READABLE = 0x00000001;
    /** chế độ tệp: world-writable. */
    public static final int MODE_WORLD_WRITEABLE = 0x00000002;
    /** chế độ tệp: append. */
    public static final int MODE_APPEND = 0x00008000;
    /** chế độ tệp: private. */
    public static final int MODE_PRIVATE = 0x00000000;

    /**
     * trả về bối cảnh ứng dụng.
     */
    public abstract Context getApplicationContext();

    /**
     * trả về tên gói.
     */
    public abstract String getPackageName();

    /**
     * trả về các tùy chọn chia sẻ của ứng dụng.
     */
    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    /**
     * trả về một dịch vụ hệ thống theo tên.
     */
    public Object getSystemService(String name) {
        if (name == null) return null;
        if (name.equals("telephony")) return new android.telephony.TelephonyManager();
        if (name.equals("bluetooth")) return android.bluetooth.BluetoothAdapter.getDefaultAdapter();
        if (name.equals("notification")) return new android.app.NotificationManager();
        if (name.equals("location")) return new android.location.LocationManager();
        if (name.equals("wifi")) return new android.net.wifi.WifiManager();
        if (name.equals("sensor")) return new android.hardware.SensorManager();
        if (name.equals("audio")) return new android.media.AudioManager();
        if (name.equals("vibrator")) return new android.os.Vibrator();
        if (name.equals("power")) return new android.os.PowerManager();
        if (name.equals("connectivity")) return new android.net.ConnectivityManager();
        if (name.equals("window")) return new android.view.WindowManager();
        if (name.equals("layout_inflater")) return new android.view.LayoutInflater();
        if (name.equals("activity")) return this;
        if (name.equals("clipboard")) return new android.content.ClipboardManager();
        if (name.equals("input_method")) return new android.view.inputmethod.InputMethodManager();
        return null;
    }

    /**
     * trả về một tài nguyên chuỗi.
     */
    public String getString(int resId) {
        return "";
    }

    /**
     * trả về một tài nguyên chuỗi với các đối số định dạng.
     */
    public String getString(int resId, Object... formatArgs) {
        return "";
    }

    /**
     * bắt đầu một hoạt động.
     */
    public void startActivity(Intent intent) {
    }

    /**
     * trả về các tài sản của ứng dụng.
     */
    public android.content.res.AssetManager getAssets() {
        return new android.content.res.AssetManager();
    }

    /**
     * trả về các tài nguyên của ứng dụng.
     */
    public android.content.res.Resources getResources() {
        return new android.content.res.Resources();
    }

    /**
     * trả về trình phân giải nội dung.
     */
    public android.content.ContentResolver getContentResolver() {
        return new android.content.ContentResolver(this);
    }

    /**
     * trả về trình lặp chính.
     */
    public android.os.Looper getMainLooper() {
        return android.os.Looper.getMainLooper();
    }

    /**
     * trả về trình quản lý gói.
     */
    public android.content.pm.PackageManager getPackageManager() {
        return new android.content.pm.PackageManager();
    }

    /**
     * trả về thông tin ứng dụng.
     */
    public android.content.pm.ApplicationInfo getApplicationInfo() {
        return new android.content.pm.ApplicationInfo();
    }

    /**
     * trả về trình tải lớp.
     */
    public ClassLoader getClassLoader() {
        return Context.class.getClassLoader();
    }

    /**
     * trả về thư mục tệp của ứng dụng.
     */
    public java.io.File getFilesDir() {
        return new java.io.File("/data/data/" + getPackageName() + "/files");
    }

    /**
     * trả về thư mục bộ nhớ cache của ứng dụng.
     */
    public java.io.File getCacheDir() {
        return new java.io.File("/data/data/" + getPackageName() + "/cache");
    }

    /**
     * trả về thư mục tệp bên ngoài của ứng dụng.
     */
    public java.io.File getExternalFilesDir(String type) {
        return new java.io.File("/sdcard/Android/data/" + getPackageName() + "/files");
    }

    /**
     * trả về đường dẫn cơ sở dữ liệu của ứng dụng.
     */
    public java.io.File getDatabasePath(String name) {
        return new java.io.File("/data/data/" + getPackageName() + "/databases/" + name);
    }

    /**
     * trả về thư mục các tùy chọn chia sẻ của ứng dụng.
     */
    public java.io.File getSharedPrefsFile(String name) {
        return new java.io.File("/data/data/" + getPackageName() + "/shared_prefs/" + name + ".xml");
    }

    /**
     * trả về thư mục obb của ứng dụng.
     */
    public java.io.File getObbDir() {
        return new java.io.File("/sdcard/Android/obb/" + getPackageName());
    }
}
