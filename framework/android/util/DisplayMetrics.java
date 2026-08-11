package android.util;

/**
 * triển khai android.util.displaymetrics tối thiểu.
 *
 * mô tả kích thước và mật độ của màn hình. đối với kudroid, các giá trị được đặt
 * từ độ phân giải màn hình ios thực tế thông qua cầu nối gốc.
 */
public class DisplayMetrics {
    /**
     * Kích thước màn hình thật của máy iOS đang chạy — được bắn từ Swift
     * (UIScreen.main.bounds) xuyên qua C++ (kudroid_set_metal_layer →
     * kudroid_jni_update_display_metrics lúc JVM khởi tạo) vào đây.
     */
    public static int sWidthPixels = 1080;
    public static int sHeightPixels = 1920;
    public static float sDensity = 3.0f;

    /** chiều rộng tuyệt đối của màn hình tính bằng pixel. */
    public int widthPixels;
    /** chiều cao tuyệt đối của màn hình tính bằng pixel. */
    public int heightPixels;
    /** mật độ logic của màn hình. */
    public float density;
    /** mật độ màn hình được biểu thị bằng số chấm trên inch. */
    public int densityDpi;
    /** số pixel vật lý chính xác trên mỗi inch của màn hình theo chiều x. */
    public float xdpi;
    /** số pixel vật lý chính xác trên mỗi inch của màn hình theo chiều y. */
    public float ydpi;
    /** mật độ màn hình được báo cáo trước khi chia tỷ lệ. */
    public float scaledDensity;

    public DisplayMetrics() {
        // Đọc từ statics (đã được native cập nhật) thay vì hardcode.
        setTo(sWidthPixels, sHeightPixels, sDensity);
    }

    /**
     * Được gọi từ native (kudroid_jni.cpp) với số liệu thật của UIScreen.
     */
    public static void updateFromNative(int width, int height, float densityValue) {
        sWidthPixels = width;
        sHeightPixels = height;
        sDensity = densityValue;
    }

    /**
     * thiết lập các số liệu hiển thị từ cầu nối gốc.
     */
    public void setTo(int width, int height, float densityValue) {
        widthPixels = width;
        heightPixels = height;
        density = densityValue;
        densityDpi = (int) (densityValue * 160.0f);
        xdpi = densityValue * 160.0f;
        ydpi = densityValue * 160.0f;
        scaledDensity = densityValue;
    }
}
