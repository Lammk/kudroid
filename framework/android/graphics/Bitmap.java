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
    private Config mApiConfig;
    private int[] mPixels;

    // Bulk pixel work runs natively: a per-pixel Java loop costs hundreds of interpreted
    // instructions per pixel, which makes a text-sized bitmap take hundreds of milliseconds.
    private static native void nativeCopyPixels(int[] src, int config, byte[] dst, int dstOffset);
    private static native void nativeFill(int[] pixels, int color);
    private static native void nativeFillRect(int[] pixels, int stride, int left, int top, int right,
                                              int bottom, int color);

    /** Reused by copies into direct buffers; the heap has no collector. */
    private static byte[] sCopyScratch = new byte[0];

    private int configCode() {
        return mApiConfig == Config.ALPHA_8 ? 1 :
               mApiConfig == Config.RGB_565 ? 2 :
               mApiConfig == Config.ARGB_4444 ? 3 : 0;
    }

    private Bitmap(int width, int height, int config) {
        if (width <= 0 || height <= 0 || (long) width * height > Integer.MAX_VALUE / 4) {
            throw new IllegalArgumentException("Invalid bitmap dimensions");
        }
        mWidth = width;
        mHeight = height;
        mConfig = config;
        mApiConfig = config == RGB_565 ? Config.RGB_565 :
                     config == ALPHA_8 ? Config.ALPHA_8 : Config.ARGB_8888;
        mPixels = new int[width * height];
    }

    private Bitmap(int width, int height, Config config) {
        this(width, height, config == Config.RGB_565 ? RGB_565 :
                            config == Config.ALPHA_8 ? ALPHA_8 : ARGB_8888);
        if (config != null) mApiConfig = config;
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
        return new Bitmap(width, height, config);
    }

    public static Bitmap createBitmap(Bitmap source) {
        if (source == null) return null;
        Bitmap copy = new Bitmap(source.mWidth, source.mHeight, source.mApiConfig);
        System.arraycopy(source.mPixels, 0, copy.mPixels, 0, source.mPixels.length);
        return copy;
    }

    public static Bitmap createBitmap(int[] colors, int width, int height, Config config) {
        Bitmap bitmap = new Bitmap(width, height, config);
        if (colors != null) System.arraycopy(colors, 0, bitmap.mPixels, 0,
                                            Math.min(colors.length, bitmap.mPixels.length));
        return bitmap;
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
        Bitmap out = new Bitmap(dstWidth, dstHeight, src.mApiConfig);
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
        final int c0 = Math.max(x, 0);
        final int c1 = Math.min(x + width, mWidth);
        if (c1 <= c0) return;
        for (int row = 0; row < height; row++) {
            final int srcY = y + row;
            if (srcY < 0 || srcY >= mHeight) continue;
            System.arraycopy(mPixels, srcY * mWidth + c0, pixels,
                    offset + row * stride + (c0 - x), c1 - c0);
        }
    }

    /**
     * set pixels from an array.
     */
    public void setPixels(int[] pixels, int offset, int stride, int x, int y,
                          int width, int height) {
        final int c0 = Math.max(x, 0);
        final int c1 = Math.min(x + width, mWidth);
        if (c1 <= c0) return;
        for (int row = 0; row < height; row++) {
            final int dstY = y + row;
            if (dstY < 0 || dstY >= mHeight) continue;
            System.arraycopy(pixels, offset + row * stride + (c0 - x), mPixels,
                    dstY * mWidth + c0, c1 - c0);
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

    public int getRowBytes() {
        return mWidth * (mApiConfig == Config.ALPHA_8 ? 1 :
                        mApiConfig == Config.RGB_565 || mApiConfig == Config.ARGB_4444 ? 2 : 4);
    }

    public int getByteCount() { return getRowBytes() * mHeight; }

    /** Fills the part of the rectangle that lies inside the bitmap. */
    void fillRect(int left, int top, int right, int bottom, int color) {
        if (mPixels == null) throw new IllegalStateException("Bitmap is recycled");
        left = Math.max(0, left);
        top = Math.max(0, top);
        right = Math.min(mWidth, right);
        bottom = Math.min(mHeight, bottom);
        if (left >= right || top >= bottom) return;
        nativeFillRect(mPixels, mWidth, left, top, right, bottom, color);
    }

    public void eraseColor(int color) {
        if (mPixels == null) throw new IllegalStateException("Bitmap is recycled");
        nativeFill(mPixels, color);
    }

    public void copyPixelsToBuffer(java.nio.Buffer destination) {
        if (mPixels == null) throw new IllegalStateException("Bitmap is recycled");
        final int shift;
        if (destination instanceof java.nio.ByteBuffer) shift = 0;
        else if (destination instanceof java.nio.ShortBuffer) shift = 1;
        else if (destination instanceof java.nio.IntBuffer) shift = 2;
        else throw new RuntimeException("Unsupported buffer type");
        if (((long) destination.remaining() << shift) < getByteCount()) {
            throw new RuntimeException("Buffer is too small");
        }
        if (mApiConfig == Config.RGBA_F16 || mApiConfig == Config.HARDWARE) {
            throw new IllegalStateException("Unsupported bitmap configuration");
        }
        if (destination instanceof java.nio.ByteBuffer) {
            final java.nio.ByteBuffer bytes = (java.nio.ByteBuffer) destination;
            final int count = getByteCount();
            if (bytes.hasArray()) {
                nativeCopyPixels(mPixels, configCode(), bytes.array(),
                        bytes.arrayOffset() + bytes.position());
                bytes.position(bytes.position() + count);
            } else {
                if (sCopyScratch.length < count) sCopyScratch = new byte[count];
                nativeCopyPixels(mPixels, configCode(), sCopyScratch, 0);
                bytes.put(sCopyScratch, 0, count);
            }
            return;
        }
        final int stride = getRowBytes() / mWidth;
        int packed = 0, used = 0;
        for (int pixel : mPixels) {
            int a = pixel >>> 24;
            int r = ((pixel >>> 16) & 255) * a / 255;
            int g = ((pixel >>> 8) & 255) * a / 255;
            int b = (pixel & 255) * a / 255;
            int value = r | (g << 8) | (b << 16) | (a << 24);
            if (mApiConfig == Config.ALPHA_8) value = a;
            else if (mApiConfig == Config.RGB_565) {
                value = (((pixel >>> 19) & 31) << 11) |
                        (((pixel >>> 10) & 63) << 5) | ((pixel >>> 3) & 31);
            } else if (mApiConfig == Config.ARGB_4444) {
                value = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
            }
            for (int i = 0; i < stride; ++i) {
                int next = (value >>> (i * 8)) & 255;
                if (shift == 0) ((java.nio.ByteBuffer) destination).put((byte) next);
                else {
                    packed |= next << (used * 8);
                    if (++used == (1 << shift)) {
                        if (shift == 1) ((java.nio.ShortBuffer) destination).put((short) packed);
                        else ((java.nio.IntBuffer) destination).put(packed);
                        packed = used = 0;
                    }
                }
            }
        }
        if (used != 0) {
            final int mask = -1 >>> (32 - used * 8);
            final int position = destination.position();
            if (shift == 1) {
                java.nio.ShortBuffer shorts = (java.nio.ShortBuffer) destination;
                shorts.put(position, (short) (packed | (shorts.get(position) & ~mask)));
            } else {
                java.nio.IntBuffer ints = (java.nio.IntBuffer) destination;
                ints.put(position, packed | (ints.get(position) & ~mask));
            }
        }
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
        return mApiConfig;
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
