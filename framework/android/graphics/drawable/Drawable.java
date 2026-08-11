package android.graphics.drawable;

/**
 * triển khai android.graphics.drawable.drawable tối thiểu.
 *
 * một drawable chung. đối với khuôn khổ tối thiểu của kudroid, đây là một
 * lớp cơ sở mô phỏng.
 */
public abstract class Drawable {
    public Drawable() {
    }

    /**
     * thiết lập ranh giới của drawable.
     */
    public void setBounds(int left, int top, int right, int bottom) {
    }

    /**
     * trả về chiều rộng thực tại của drawable.
     */
    public int getIntrinsicWidth() {
        return 0;
    }

    /**
     * trả về chiều cao thực tại của drawable.
     */
    public int getIntrinsicHeight() {
        return 0;
    }

    /**
     * vẽ drawable lên một canvas.
     */
    public void draw(android.graphics.Canvas canvas) {
    }

    /**
     * thiết lập alpha của drawable.
     */
    public void setAlpha(int alpha) {
    }

    /**
     * thiết lập bộ lọc màu của drawable.
     */
    public void setColorFilter(android.graphics.ColorFilter colorFilter) {
    }

    /**
     * trả về độ mờ của drawable.
     */
    public int getOpacity() {
        return android.graphics.PixelFormat.OPAQUE;
    }
}
