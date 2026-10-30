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

    private static native int[] nativeDecodeByteArray(byte[] data, int offset, int length,
                                                      int[] outDim, boolean justBounds);

    public static Bitmap decodeFile(String pathName) {
        return decodeFile(pathName, null);
    }

    public static Bitmap decodeFile(String pathName, Options opts) {
        if (pathName == null) {
            markUndecodable(opts);
            return null;
        }
        try (java.io.FileInputStream fis = new java.io.FileInputStream(pathName)) {
            return decodeStream(fis, null, opts);
        } catch (Throwable t) {
            markUndecodable(opts);
            return null;
        }
    }

    public static Bitmap decodeStream(java.io.InputStream is) {
        return decodeStream(is, null, null);
    }

    public static Bitmap decodeStream(java.io.InputStream is, Rect outPadding, Options opts) {
        if (is == null) {
            markUndecodable(opts);
            return null;
        }
        try {
            java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int r;
            while ((r = is.read(buf)) != -1) {
                baos.write(buf, 0, r);
            }
            byte[] bytes = baos.toByteArray();
            return decodeByteArray(bytes, 0, bytes.length, opts);
        } catch (Throwable t) {
            markUndecodable(opts);
            return null;
        }
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length) {
        return decodeByteArray(data, offset, length, null);
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length, Options opts) {
        if (data == null || offset < 0 || length <= 0 || offset + length > data.length) {
            markUndecodable(opts);
            return null;
        }
        boolean justBounds = (opts != null && opts.inJustDecodeBounds);
        int[] outDim = new int[2];
        try {
            int[] pixels = nativeDecodeByteArray(data, offset, length, outDim, justBounds);
            if (outDim[0] <= 0 || outDim[1] <= 0) {
                markUndecodable(opts);
                return null;
            }
            if (opts != null) {
                opts.outWidth = outDim[0];
                opts.outHeight = outDim[1];
            }
            if (justBounds || pixels == null) {
                return null;
            }
            return Bitmap.createBitmap(pixels, outDim[0], outDim[1], Bitmap.Config.ARGB_8888);
        } catch (Throwable t) {
            markUndecodable(opts);
            return null;
        }
    }

    public static Bitmap decodeResource(android.content.res.Resources res, int id) {
        return decodeResource(res, id, null);
    }

    public static Bitmap decodeResource(android.content.res.Resources res, int id, Options opts) {
        if (res == null) {
            markUndecodable(opts);
            return null;
        }
        try {
            android.util.TypedValue value = new android.util.TypedValue();
            res.getValue(id, value, true);
            if (value.string != null) {
                String path = value.string.toString();
                byte[] data = res.readResource(path);
                if (data != null && data.length > 0) {
                    return decodeByteArray(data, 0, data.length, opts);
                }
            }
        } catch (Throwable ignored) {}
        markUndecodable(opts);
        return null;
    }

    public static Bitmap decodeFileDescriptor(java.io.FileDescriptor fd) {
        return decodeFileDescriptor(fd, null, null);
    }

    public static Bitmap decodeFileDescriptor(java.io.FileDescriptor fd, Rect outPadding,
                                              Options opts) {
        if (fd == null) {
            markUndecodable(opts);
            return null;
        }
        try (java.io.FileInputStream fis = new java.io.FileInputStream(fd)) {
            return decodeStream(fis, outPadding, opts);
        } catch (Throwable t) {
            markUndecodable(opts);
            return null;
        }
    }

    private static void markUndecodable(Options opts) {
        if (opts == null) return;
        opts.outWidth = -1;
        opts.outHeight = -1;
        opts.outMimeType = null;
    }
}
