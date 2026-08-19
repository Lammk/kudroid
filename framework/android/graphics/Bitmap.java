package android.graphics;

/**
 * triển khai android.graphics.bitmap tối thiểu.
 *
 * đại diện cho một hình ảnh bitmap. đối với khuôn khổ tối thiểu của kudroid, nó lưu trữ
 * chiều rộng/chiều cao và bộ đệm pixel.
 */
public final class Bitmap {
    /** cấu hình bitmap: argb_8888. */
    public static final int ARGB_8888 = 0;
    /** cấu hình bitmap: rgb_565. */
    public static final int RGB_565 = 1;
    /** cấu hình bitmap: alpha_8. */
    public static final int ALPHA_8 = 2;

    private final int mWidth;
    private final int mHeight;
    private final int mConfig;
    private int[] mPixels;

    private Bitmap(int width, int height, int config) {
        mWidth = width;
        mHeight = height;
        mConfig = config;
        mPixels = new int[width * height];
    }

    /**
     * tạo một bitmap.
     */
    public static Bitmap createBitmap(int width, int height, int config) {
        return new Bitmap(width, height, config);
    }

    /**
     * tạo một bitmap từ một mảng pixel.
     */
    public static Bitmap createBitmap(int[] colors, int width, int height, int config) {
        Bitmap b = new Bitmap(width, height, config);
        if (colors != null) {
            System.arraycopy(colors, 0, b.mPixels, 0, Math.min(colors.length, b.mPixels.length));
        }
        return b;
    }

    /**
     * trả về chiều rộng bitmap.
     */
    public int getWidth() {
        return mWidth;
    }

    /**
     * trả về chiều cao bitmap.
     */
    public int getHeight() {
        return mHeight;
    }

    /**
     * trả về cấu hình bitmap.
     */
    public int getConfig() {
        return mConfig;
    }

    /**
     * trả về pixel tại (x, y).
     */
    public int getPixel(int x, int y) {
        if (x < 0 || y < 0 || x >= mWidth || y >= mHeight) return 0;
        return mPixels[y * mWidth + x];
    }

    /**
     * thiết lập pixel tại (x, y).
     */
    public void setPixel(int x, int y, int color) {
        if (x < 0 || y < 0 || x >= mWidth || y >= mHeight) return;
        mPixels[y * mWidth + x] = color;
    }

    /**
     * sao chép các pixel vào một mảng.
     */
    public void getPixels(int[] pixels, int offset, int stride, int x, int y,
                          int width, int height) {
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                int srcX = x + col;
                int srcY = y + row;
                if (srcX >= 0 && srcY >= 0 && srcX < mWidth && srcY < mHeight) {
                    pixels[offset + row * stride + col] = mPixels[srcY * mWidth + srcX];
                }
            }
        }
    }

    /**
     * thiết lập các pixel từ một mảng.
     */
    public void setPixels(int[] pixels, int offset, int stride, int x, int y,
                          int width, int height) {
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                int dstX = x + col;
                int dstY = y + row;
                if (dstX >= 0 && dstY >= 0 && dstX < mWidth && dstY < mHeight) {
                    mPixels[dstY * mWidth + dstX] = pixels[offset + row * stride + col];
                }
            }
        }
    }

    /**
     * tái chế bitmap.
     */
    public void recycle() {
        mPixels = null;
    }

    /**
     * trả về việc bitmap đã được tái chế hay chưa.
     */
    public boolean isRecycled() {
        return mPixels == null;
    }

    public int[] getPixels() {
        return mPixels;
    }
}
