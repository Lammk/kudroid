package android.graphics;

/**
 * triển khai android.graphics.canvas tối thiểu.
 *
 * cung cấp một bề mặt vẽ. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng
 * ghi lại các thao tác vẽ (để được kết nối với metal/angle sau này).
 */
public class Canvas {
    private Bitmap mBitmap;
    private int mWidth;
    private int mHeight;

    public Canvas() {
    }

    public Canvas(Bitmap bitmap) {
        mBitmap = bitmap;
        if (bitmap != null) {
            mWidth = bitmap.getWidth();
            mHeight = bitmap.getHeight();
        }
    }

    /**
     * trả về chiều rộng canvas.
     */
    public int getWidth() {
        return mWidth;
    }

    /**
     * trả về chiều cao canvas.
     */
    public int getHeight() {
        return mHeight;
    }

    /**
     * vẽ một màu.
     */
    public void drawColor(int color) {
    }

    /**
     * vẽ một màu với chế độ porter-duff.
     */
    public void drawColor(int color, PorterDuff.Mode mode) {
    }

    /**
     * vẽ một bitmap.
     */
    public void drawBitmap(Bitmap bitmap, float left, float top, Paint paint) {
    }

    /**
     * vẽ một hình chữ nhật.
     */
    public void drawRect(float left, float top, float right, float bottom, Paint paint) {
    }

    /**
     * vẽ một hình chữ nhật.
     */
    public void drawRect(Rect rect, Paint paint) {
    }

    /**
     * vẽ một hình tròn.
     */
    public void drawCircle(float cx, float cy, float radius, Paint paint) {
    }

    /**
     * vẽ một đường thẳng.
     */
    public void drawLine(float startX, float startY, float stopX, float stopY, Paint paint) {
    }

    /**
     * vẽ văn bản.
     */
    public void drawText(String text, float x, float y, Paint paint) {
    }

    /**
     * lưu trạng thái canvas.
     */
    public int save() {
        return 0;
    }

    /**
     * khôi phục trạng thái canvas.
     */
    public void restore() {
    }

    /**
     * dịch chuyển canvas.
     */
    public void translate(float dx, float dy) {
    }

    /**
     * chia tỷ lệ canvas.
     */
    public void scale(float sx, float sy) {
    }

    /**
     * xoay canvas.
     */
    public void rotate(float degrees) {
    }

    /**
     * cắt theo một hình chữ nhật.
     */
    public boolean clipRect(float left, float top, float right, float bottom) {
        return true;
    }

    /**
     * trả về bitmap của canvas.
     */
    public Bitmap getBitmap() {
        return mBitmap;
    }
}
