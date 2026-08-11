package android.content.res;

/**
 * triển khai android.content.res.assetmanager tối thiểu.
 *
 * cung cấp quyền truy cập vào các tài sản được đóng gói của ứng dụng. đối với khuôn khổ tối thiểu của kudroid,
 * đây là một mô phỏng trả về null/trống cho các tra cứu tài sản.
 */
public final class AssetManager {
    public AssetManager() {
    }

    /**
     * mở một tệp nội dung. hiện tại trả về null (không tìm thấy).
     */
    public java.io.InputStream open(String fileName) throws java.io.IOException {
        throw new java.io.FileNotFoundException("Asset not found: " + fileName);
    }

    /**
     * mở một tệp nội dung với chế độ truy cập. hiện tại trả về null.
     */
    public java.io.InputStream open(String fileName, int accessMode) throws java.io.IOException {
        return open(fileName);
    }

    /**
     * liệt kê các tài sản trong một thư mục. hiện tại trả về mảng trống.
     */
    public String[] list(String path) {
        return new String[0];
    }
}
