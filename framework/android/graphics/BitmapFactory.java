package android.graphics;

/**
 * android.graphics.BitmapFactory.
 *
 * Options was an empty stub, so `opts.inJustDecodeBounds = true` — the standard way
 * to measure an image before allocating for it — threw NoSuchFieldError. The fields
 * are public and both written and read directly by callers, so they must exist under
 * their exact AOSP names.
 */
public class BitmapFactory {

    public BitmapFactory() {}

    public static class Options {
        /**
         * Measure only: decode* returns null and fills in outWidth/outHeight.
         *
         * Apps use this to pick inSampleSize before committing memory, so it has to
         * be honoured by whatever decoder KuDroid grows later.
         */
        public boolean inJustDecodeBounds;

        /** Subsampling factor; 1 or less means full resolution. */
        public int inSampleSize = 1;

        /** Result the decoder should produce. */
        public Bitmap.Config inPreferredConfig = Bitmap.Config.ARGB_8888;

        public boolean inPremultiplied = true;
        public boolean inMutable;
        public boolean inScaled = true;
        public boolean inDither;

        public int inDensity;
        public int inTargetDensity;
        public int inScreenDensity;

        /** Reported size; set even when inJustDecodeBounds is false. */
        public int outWidth;
        public int outHeight;
        public String outMimeType;
        public Bitmap.Config outConfig;

        /** Set to true to abort an in-flight decode. */
        public boolean mCancel;

        public byte[] inTempStorage;

        public Options() {}

        public void requestCancelDecode() {
            mCancel = true;
        }
    }

    /**
     * Decoders.
     *
     * KuDroid has no image decoder, so these report failure by returning null — which
     * is exactly what Android does for undecodable input, and callers already handle
     * it. outWidth/outHeight are set to -1 to match the platform's "nothing decoded"
     * convention rather than leaving a misleading 0.
     */
    public static Bitmap decodeFile(String pathName) {
        return decodeFile(pathName, null);
    }

    public static Bitmap decodeFile(String pathName, Options opts) {
        markUndecodable(opts);
        return null;
    }

    public static Bitmap decodeStream(java.io.InputStream is) {
        return decodeStream(is, null, null);
    }

    public static Bitmap decodeStream(java.io.InputStream is, Rect outPadding, Options opts) {
        markUndecodable(opts);
        return null;
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length) {
        return decodeByteArray(data, offset, length, null);
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length, Options opts) {
        markUndecodable(opts);
        return null;
    }

    public static Bitmap decodeResource(android.content.res.Resources res, int id) {
        return decodeResource(res, id, null);
    }

    public static Bitmap decodeResource(android.content.res.Resources res, int id, Options opts) {
        markUndecodable(opts);
        return null;
    }

    public static Bitmap decodeFileDescriptor(java.io.FileDescriptor fd) {
        return decodeFileDescriptor(fd, null, null);
    }

    public static Bitmap decodeFileDescriptor(java.io.FileDescriptor fd, Rect outPadding,
                                              Options opts) {
        markUndecodable(opts);
        return null;
    }

    private static void markUndecodable(Options opts) {
        if (opts == null) return;
        opts.outWidth = -1;
        opts.outHeight = -1;
        opts.outMimeType = null;
    }
}
