package android.graphics;

/**
 * Minimal android.graphics.Bitmap implementation.
 *
 * Represents a bitmap image. For KuDroid's minimal framework, this stores
 * width/height and a pixel buffer.
 */
public final class Bitmap {
    /** Bitmap config: ARGB_8888. */
    public static final int ARGB_8888 = 0;
    /** Bitmap config: RGB_565. */
    public static final int RGB_565 = 1;
    /** Bitmap config: ALPHA_8. */
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
     * Create a bitmap.
     */
    public static Bitmap createBitmap(int width, int height, int config) {
        return new Bitmap(width, height, config);
    }

    /**
     * Create a bitmap from a pixel array.
     */
    public static Bitmap createBitmap(int[] colors, int width, int height, int config) {
        Bitmap b = new Bitmap(width, height, config);
        if (colors != null) {
            System.arraycopy(colors, 0, b.mPixels, 0, Math.min(colors.length, b.mPixels.length));
        }
        return b;
    }

    /**
     * Return the bitmap width.
     */
    public int getWidth() {
        return mWidth;
    }

    /**
     * Return the bitmap height.
     */
    public int getHeight() {
        return mHeight;
    }

    /**
     * Return the bitmap config.
     */
    public int getConfig() {
        return mConfig;
    }

    /**
     * Return the pixel at (x, y).
     */
    public int getPixel(int x, int y) {
        if (x < 0 || y < 0 || x >= mWidth || y >= mHeight) return 0;
        return mPixels[y * mWidth + x];
    }

    /**
     * Set the pixel at (x, y).
     */
    public void setPixel(int x, int y, int color) {
        if (x < 0 || y < 0 || x >= mWidth || y >= mHeight) return;
        mPixels[y * mWidth + x] = color;
    }

    /**
     * Copy pixels into an array.
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
     * Set pixels from an array.
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
     * Recycle the bitmap.
     */
    public void recycle() {
        mPixels = null;
    }

    /**
     * Return whether the bitmap has been recycled.
     */
    public boolean isRecycled() {
        return mPixels == null;
    }
}
