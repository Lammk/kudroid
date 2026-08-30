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
     * create a bitmap with a Config rather than an int.
     *
     * This is the signature apps actually reference —
     * {@code createBitmap(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;} —
     * and all five real APKs in the corpus need it. The int form below is the internal one;
     * having only that left every ordinary createBitmap call unresolved.
     */
    public static Bitmap createBitmap(int width, int height, Config config) {
        return new Bitmap(width, height, config != null ? config.nativeInt()
                                                        : Config.ARGB_8888.nativeInt());
    }

    public static Bitmap createBitmap(Bitmap source) {
        if (source == null) return null;
        Bitmap copy = new Bitmap(source.mWidth, source.mHeight, source.mConfig);
        System.arraycopy(source.mPixels, 0, copy.mPixels, 0, source.mPixels.length);
        return copy;
    }

    public static Bitmap createBitmap(int[] colors, int width, int height, Config config) {
        return createBitmap(colors, width, height,
                            config != null ? config.nativeInt() : Config.ARGB_8888.nativeInt());
    }

    /**
     * Scale `src` to the given size.
     *
     * Nearest-neighbour, chosen deliberately over bilinear: the callers in the corpus scale
     * icons and thumbnails where a visibly blocky result is obvious, while a smoothing
     * filter that is subtly wrong is not. `filter` is accepted and ignored, which is the
     * honest reading of "no filtering implemented".
     */
    public static Bitmap createScaledBitmap(Bitmap src, int dstWidth, int dstHeight,
                                            boolean filter) {
        if (src == null || dstWidth <= 0 || dstHeight <= 0) return null;
        Bitmap out = new Bitmap(dstWidth, dstHeight, src.mConfig);
        if (src.mWidth == 0 || src.mHeight == 0) return out;
        for (int y = 0; y < dstHeight; ++y) {
            final int srcY = (int) ((long) y * src.mHeight / dstHeight);
            for (int x = 0; x < dstWidth; ++x) {
                final int srcX = (int) ((long) x * src.mWidth / dstWidth);
                out.mPixels[y * dstWidth + x] = src.mPixels[srcY * src.mWidth + srcX];
            }
        }
        return out;
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
     * The int config this Bitmap was created with.
     *
     * Not named getConfig(): Android's getConfig() returns Bitmap.Config, and having
     * an int-returning method under that name meant app code calling the real
     * signature got a type it could not use. The int form is kept for the existing
     * KuDroid callers that construct with the int constants.
     */
    public int getConfigInt() {
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

    /**
     * Pixel format.
     *
     * An enum on Android, and apps use the constants as values (passing them to
     * createBitmap, comparing against getConfig()). It was an empty stub, so
     * `Bitmap.Config.ARGB_8888` threw NoSuchFieldError. Modelled as final instances
     * rather than a Java enum so the identity comparisons apps make still work
     * without pulling in enum machinery.
     */
    public static final class Config {
        public static final Config ALPHA_8 = new Config("ALPHA_8", 1);
        public static final Config RGB_565 = new Config("RGB_565", 3);
        public static final Config ARGB_4444 = new Config("ARGB_4444", 4);
        public static final Config ARGB_8888 = new Config("ARGB_8888", 5);
        public static final Config RGBA_F16 = new Config("RGBA_F16", 6);
        public static final Config HARDWARE = new Config("HARDWARE", 7);

        private final String mName;
        private final int mValue;

        private Config(String name, int value) {
            mName = name;
            mValue = value;
        }

        public int nativeInt() { return mValue; }

        public String name() { return mName; }

        public int ordinal() { return mValue - 1; }

        public static Config valueOf(String name) {
            if (name == null) return ARGB_8888;
            if (name.equals("ALPHA_8")) return ALPHA_8;
            if (name.equals("RGB_565")) return RGB_565;
            if (name.equals("ARGB_4444")) return ARGB_4444;
            if (name.equals("RGBA_F16")) return RGBA_F16;
            if (name.equals("HARDWARE")) return HARDWARE;
            return ARGB_8888;
        }

        public static Config[] values() {
            return new Config[] { ALPHA_8, RGB_565, ARGB_4444, ARGB_8888, RGBA_F16, HARDWARE };
        }

        @Override
        public String toString() { return mName; }
    }

    /** Format for compress(); same reasoning as Config. */
    public static final class CompressFormat {
        public static final CompressFormat JPEG = new CompressFormat("JPEG", 0);
        public static final CompressFormat PNG = new CompressFormat("PNG", 1);
        public static final CompressFormat WEBP = new CompressFormat("WEBP", 2);
        public static final CompressFormat WEBP_LOSSY = new CompressFormat("WEBP_LOSSY", 3);
        public static final CompressFormat WEBP_LOSSLESS =
                new CompressFormat("WEBP_LOSSLESS", 4);

        private final String mName;
        private final int mValue;

        private CompressFormat(String name, int value) {
            mName = name;
            mValue = value;
        }

        public int nativeInt() { return mValue; }
        public String name() { return mName; }
        public int ordinal() { return mValue; }

        public static CompressFormat[] values() {
            return new CompressFormat[] { JPEG, PNG, WEBP, WEBP_LOSSY, WEBP_LOSSLESS };
        }

        @Override
        public String toString() { return mName; }
    }

    /**
     * Android's getConfig(), returning the Config instance.
     *
     * Maps the int the Bitmap was built with; anything unrecognised reports
     * ARGB_8888, the format KuDroid's canvas actually uses.
     */
    public Config getConfig() {
        if (mConfig == RGB_565) return Config.RGB_565;
        if (mConfig == ALPHA_8) return Config.ALPHA_8;
        return Config.ARGB_8888;
    }

    /**
     * Encode the bitmap.
     *
     * KuDroid ships no encoder, so this reports failure rather than writing a
     * corrupt file — callers check the boolean and a false is a case they handle.
     */
    public boolean compress(CompressFormat format, int quality, java.io.OutputStream stream) {
        return false;
    }

}
