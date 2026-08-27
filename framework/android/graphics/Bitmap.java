package android.graphics;

/**
 * minimal android.graphics.bitmap implementation.
 *
 * represents a bitmap image. for kudroid minimal framework it stores
 * width/height and pixel buffer.
 */
public final class Bitmap {
    /** bitmap configuration: argb_8888. */
    public static final int ARGB_8888 = 0;
    /** bitmap configuration: rgb_565. */
    public static final int RGB_565 = 1;
    /** bitmap configuration: alpha_8. */
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
     * create a bitmap.
     */
    public static Bitmap createBitmap(int width, int height, int config) {
        return new Bitmap(width, height, config);
    }

    /**
     * create a bitmap from an array of pixels.
     */
    public static Bitmap createBitmap(int[] colors, int width, int height, int config) {
        Bitmap b = new Bitmap(width, height, config);
        if (colors != null) {
            System.arraycopy(colors, 0, b.mPixels, 0, Math.min(colors.length, b.mPixels.length));
        }
        return b;
    }

    /**
     * returns bitmap width.
     */
    public int getWidth() {
        return mWidth;
    }

    /**
     * returns bitmap height.
     */
    public int getHeight() {
        return mHeight;
    }

    /**
     * returns bitmap configuration.
     */
    public int getConfig() {
        return mConfig;
    }

    /**
     * returns pixels at (x, y).
     */
    public int getPixel(int x, int y) {
        if (x < 0 || y < 0 || x >= mWidth || y >= mHeight) return 0;
        return mPixels[y * mWidth + x];
    }

    /**
     * set pixels at (x, y).
     */
    public void setPixel(int x, int y, int color) {
        if (x < 0 || y < 0 || x >= mWidth || y >= mHeight) return;
        mPixels[y * mWidth + x] = color;
    }

    /**
     * copies the pixels into an array.
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
     * set pixels from an array.
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
     * bitmap recycling.
     */
    public void recycle() {
        mPixels = null;
    }

    /**
     * returns whether the bitmap has been recycled or not.
     */
    public boolean isRecycled() {
        return mPixels == null;
    }

    public int[] getPixels() {
        return mPixels;
    }

    public static class Config {
        public Config() {}
    }

}
