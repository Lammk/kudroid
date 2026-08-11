package android.content.res;

/**
 * triển khai android.content.res.resources tối thiểu.
 *
 * cung cấp quyền truy cập vào các tài nguyên ứng dụng (chuỗi, kích thước, màu sắc). đối với
 * khuôn khổ tối thiểu của kudroid, hầu hết các tra cứu đều trả về giá trị mặc định.
 */
public class Resources {
    public Resources() {
    }

    /**
     * trả về một tài nguyên chuỗi. hiện tại trả về chuỗi trống.
     */
    public String getString(int id) {
        return "";
    }

    /**
     * trả về một tài nguyên chuỗi với các đối số định dạng.
     */
    public String getString(int id, Object... formatArgs) {
        return "";
    }

    /**
     * trả về một tài nguyên màu sắc. hiện tại trả về 0.
     */
    public int getColor(int id) {
        return 0;
    }

    /**
     * trả về một tài nguyên kích thước tính bằng pixel. hiện tại trả về 0.
     */
    public float getDimension(int id) {
        return 0.0f;
    }

    /**
     * trả về một tài nguyên số nguyên. hiện tại trả về 0.
     */
    public int getInteger(int id) {
        return 0;
    }

    /**
     * trả về một tài nguyên boolean. hiện tại trả về false.
     */
    public boolean getBoolean(int id) {
        return false;
    }

    /**
     * trả về các số liệu hiển thị.
     */
    public android.util.DisplayMetrics getDisplayMetrics() {
        return new android.util.DisplayMetrics();
    }

    /**
     * trả về cấu hình.
     */
    public android.content.res.Configuration getConfiguration() {
        return new android.content.res.Configuration();
    }

    /**
     * trả về trình quản lý tài sản.
     */
    public AssetManager getAssets() {
        return new AssetManager();
    }
}
