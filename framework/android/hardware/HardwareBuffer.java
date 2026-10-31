package android.hardware;

import android.os.Parcel;
import android.os.Parcelable;

public final class HardwareBuffer implements Parcelable, AutoCloseable {
    public static final int RGBA_8888 = 1;
    public static final int RGBX_8888 = 2;
    public static final int RGB_888 = 3;
    public static final int RGB_565 = 4;
    public static final int RGBA_FP16 = 0x16;
    public static final int RGBA_1010102 = 0x2b;
    public static final int BLOB = 0x21;
    public static final int D_16 = 0x30;
    public static final int D_24 = 0x31;
    public static final int DS_24UI8 = 0x32;
    public static final int D_FP32 = 0x33;
    public static final int DS_FP32UI8 = 0x34;
    public static final int S_UI8 = 0x35;
    public static final long USAGE_CPU_READ_RARELY = 2;
    public static final long USAGE_CPU_READ_OFTEN = 3;
    public static final long USAGE_CPU_WRITE_RARELY = 2L << 4;
    public static final long USAGE_CPU_WRITE_OFTEN = 3L << 4;
    public static final long USAGE_GPU_SAMPLED_IMAGE = 1L << 8;
    public static final long USAGE_GPU_COLOR_OUTPUT = 1L << 9;
    public static final long USAGE_PROTECTED_CONTENT = 1L << 14;
    public static final long USAGE_VIDEO_ENCODE = 1L << 16;
    public static final long USAGE_SENSOR_DIRECT_DATA = 1L << 23;
    public static final long USAGE_GPU_DATA_BUFFER = 1L << 24;
    public static final long USAGE_GPU_CUBE_MAP = 1L << 25;
    public static final long USAGE_GPU_MIPMAP_COMPLETE = 1L << 26;

    private long mNativeObject;

    private HardwareBuffer(long nativeObject) {
        throw new UnsupportedOperationException("Native hardware buffers are unavailable");
    }

    private static void validate(int width, int height, int format, int layers) {
        switch (format) {
            case RGBA_8888: case RGBX_8888: case RGB_888: case RGB_565:
            case RGBA_FP16: case RGBA_1010102: case BLOB: case D_16: case D_24:
            case DS_24UI8: case D_FP32: case DS_FP32UI8: case S_UI8: break;
            default: throw new IllegalArgumentException("Invalid pixel format " + format);
        }
        if (width <= 0 || height <= 0 || layers <= 0)
            throw new IllegalArgumentException("Invalid buffer dimensions or layer count");
        if (format == BLOB && height != 1)
            throw new IllegalArgumentException("Height must be 1 for BLOB buffers");
    }

    public static boolean isSupported(int width, int height, int format, int layers, long usage) {
        validate(width, height, format, layers);
        return false;
    }

    public static HardwareBuffer create(int width, int height, int format, int layers, long usage) {
        validate(width, height, format, layers);
        throw new IllegalArgumentException("Unable to create a HardwareBuffer");
    }

    private void checkOpen() {
        if (isClosed()) throw new IllegalStateException("HardwareBuffer is closed");
        throw new UnsupportedOperationException("Native hardware buffers are unavailable");
    }

    public int getWidth() { checkOpen(); return 0; }
    public int getHeight() { checkOpen(); return 0; }
    public int getFormat() { checkOpen(); return 0; }
    public int getLayers() { checkOpen(); return 0; }
    public long getUsage() { checkOpen(); return 0; }
    public boolean isClosed() { return mNativeObject == 0; }
    public void close() { mNativeObject = 0; }
    public void destroy() { close(); }
    public boolean isDestroyed() { return isClosed(); }
    public int describeContents() { return Parcelable.CONTENTS_FILE_DESCRIPTOR; }
    public void writeToParcel(Parcel dest, int flags) { checkOpen(); }

    public static final Parcelable.Creator<HardwareBuffer> CREATOR =
            new Parcelable.Creator<HardwareBuffer>() {
        public HardwareBuffer createFromParcel(Parcel source) {
            throw new UnsupportedOperationException("Native hardware buffers are unavailable");
        }
        public HardwareBuffer[] newArray(int size) { return new HardwareBuffer[size]; }
    };
}
