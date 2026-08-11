package android.util;

/**
 * triển khai android.util.displaymetrics tối thiểu.
 *
 * mô tả kích thước và mật độ của màn hình. đối với kudroid, các giá trị được đặt
 * từ độ phân giải màn hình ios thực tế thông qua cầu nối gốc.
 */
public class DisplayMetrics {
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
        // Defaults; the native bridge updates these from the real screen.
        widthPixels = 1080;
        heightPixels = 1920;
        density = 3.0f;
        densityDpi = 420;
        xdpi = 420.0f;
        ydpi = 420.0f;
        scaledDensity = 3.0f;
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
